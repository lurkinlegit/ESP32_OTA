#!/bin/bash
# =============================================================================
# build_and_release.sh — Build firmware and generate manifest.json
# =============================================================================
# Usage:   ./scripts/build_and_release.sh <build_number> <version_string>
# Example: ./scripts/build_and_release.sh 2 "1.1.0"
#
# What it does:
#   1. Compiles firmware with PlatformIO
#   2. Copies firmware.bin to release/firmware-v<VERSION>-b<BUILD>.bin
#   3. Computes SHA-256 and file size
#   4. Writes release/manifest.json with all fields
#   5. Prints a summary
# =============================================================================

set -e  # Exit immediately on any error

# Ensure PlatformIO CLI is in PATH
export PATH="$HOME/.platformio/penv/bin:$PATH"

# ── Argument validation ────────────────────────────────────────────────────────
if [ "$#" -ne 2 ]; then
    echo "ERROR: Wrong number of arguments."
    echo "Usage: $0 <build_number> <version_string>"
    echo "Example: $0 2 \"1.1.0\""
    exit 1
fi

BUILD_NUMBER="$1"
VERSION_STRING="$2"

# Validate build number is a positive integer
if ! [[ "$BUILD_NUMBER" =~ ^[0-9]+$ ]] || [ "$BUILD_NUMBER" -lt 1 ]; then
    echo "ERROR: build_number must be a positive integer (e.g. 1, 2, 3)"
    exit 1
fi

# ── Paths ──────────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
PIO_OUTPUT="$PROJECT_DIR/.pio/build/esp32dev/firmware.bin"
RELEASE_DIR="$PROJECT_DIR/release"
FIRMWARE_NAME="firmware-v${VERSION_STRING}-b${BUILD_NUMBER}.bin"
FIRMWARE_DEST="$RELEASE_DIR/$FIRMWARE_NAME"
MANIFEST_FILE="$RELEASE_DIR/manifest.json"

# ── Header ────────────────────────────────────────────────────────────────────
echo "============================================================"
echo "  ESP32 FOTA Build & Release Script"
echo "  Build: $BUILD_NUMBER    Version: $VERSION_STRING"
echo "============================================================"
echo ""

# ── Step 1: PlatformIO Build ──────────────────────────────────────────────────
echo "[1/4] Building firmware with PlatformIO..."
cd "$PROJECT_DIR"
pio run -e esp32dev

if [ ! -f "$PIO_OUTPUT" ]; then
    echo "ERROR: Build succeeded but firmware.bin not found at:"
    echo "       $PIO_OUTPUT"
    exit 1
fi
echo "      Build OK. firmware.bin found."
echo ""

# ── Step 2: Copy to release/ ──────────────────────────────────────────────────
echo "[2/4] Copying firmware to release directory..."
mkdir -p "$RELEASE_DIR"
cp "$PIO_OUTPUT" "$FIRMWARE_DEST"
echo "      Saved as: $FIRMWARE_DEST"
echo ""

# ── Step 3: Compute SHA-256 and size ──────────────────────────────────────────
echo "[3/4] Computing SHA-256 and file size..."
SHA256=$(sha256sum "$FIRMWARE_DEST" | awk '{print $1}')
FILESIZE=$(stat -c%s "$FIRMWARE_DEST")
echo "      Size:   $FILESIZE bytes"
echo "      SHA256: $SHA256"
echo ""

# ── Step 4: Write manifest.json ───────────────────────────────────────────────
echo "[4/4] Writing manifest.json..."

# Read current FOTA_MANIFEST_URL from app_config.h for reference
CURRENT_URL=$(grep 'FOTA_MANIFEST_URL' "$PROJECT_DIR/include/app_config.h" \
              | grep '#define' | sed 's/.*"\(.*\)".*/\1/')

cat > "$MANIFEST_FILE" <<EOF
{
  "version": "$VERSION_STRING",
  "build": $BUILD_NUMBER,
  "size": $FILESIZE,
  "sha256": "$SHA256",
  "url": "REPLACE_WITH_SERVER_IP/$FIRMWARE_NAME",
  "port": 8080
}
EOF

echo "      manifest.json written."
echo ""

# ── Summary ───────────────────────────────────────────────────────────────────
echo "============================================================"
echo "  RELEASE SUMMARY"
echo "============================================================"
echo "  Firmware : $FIRMWARE_DEST"
echo "  Manifest : $MANIFEST_FILE"
echo "  Version  : $VERSION_STRING"
echo "  Build    : $BUILD_NUMBER"
echo "  Size     : $FILESIZE bytes"
echo "  SHA256   : $SHA256"
echo ""
echo "  NEXT STEPS:"
echo "  1. Run: ./scripts/serve_release.sh"
echo "     This prints your laptop IP address."
echo "  2. Edit release/manifest.json:"
echo "     Replace 'REPLACE_WITH_SERVER_IP' with the printed IP."
echo "  3. Also update FOTA_MANIFEST_URL in include/app_config.h"
echo "     to the same IP before flashing Version 1 to the board."
echo "============================================================"
