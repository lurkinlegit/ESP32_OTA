#!/bin/bash
# =============================================================================
# serve_release.sh — Serve the release/ folder as a local HTTP server
# =============================================================================
# Usage:   ./scripts/serve_release.sh [port]
# Example: ./scripts/serve_release.sh 8080
#          ./scripts/serve_release.sh          (defaults to port 8080)
#
# What it does:
#   1. Detects your machine's local IP address (the one on the 192.168.x.x
#      or 10.x.x.x network that the board's Airtel SIM can reach via hotspot)
#   2. Prints the exact URL to put in manifest.json and app_config.h
#   3. Starts a Python HTTP server serving files from ./release/
#   4. Ctrl+C to stop
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
RELEASE_DIR="$PROJECT_DIR/release"
PORT="${1:-8080}"

# ── Check release/ directory exists ──────────────────────────────────────────
if [ ! -d "$RELEASE_DIR" ]; then
    echo "ERROR: release/ directory not found."
    echo "       Run ./scripts/build_and_release.sh first."
    exit 1
fi

# Check that at least one .bin file exists
BIN_COUNT=$(find "$RELEASE_DIR" -name "*.bin" | wc -l)
if [ "$BIN_COUNT" -eq 0 ]; then
    echo "WARNING: No .bin files found in release/"
    echo "         Run ./scripts/build_and_release.sh to generate firmware."
fi

# ── Detect local IP ──────────────────────────────────────────────────────────
# Try common methods to get the non-loopback LAN IP
LOCAL_IP=""

# Method 1: hostname -I (Linux)
if command -v hostname &>/dev/null; then
    LOCAL_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
fi

# Method 2: ip route (fallback)
if [ -z "$LOCAL_IP" ] && command -v ip &>/dev/null; then
    LOCAL_IP=$(ip route get 8.8.8.8 2>/dev/null | awk '/src/{print $7}' | head -1)
fi

# Method 3: ifconfig (fallback)
if [ -z "$LOCAL_IP" ] && command -v ifconfig &>/dev/null; then
    LOCAL_IP=$(ifconfig 2>/dev/null | grep 'inet ' | grep -v '127.0.0.1' \
               | awk '{print $2}' | head -1 | sed 's/addr://')
fi

if [ -z "$LOCAL_IP" ]; then
    LOCAL_IP="<YOUR_IP_HERE>"
    echo "WARNING: Could not auto-detect local IP. Fill in manually."
fi

# ── Print instructions ────────────────────────────────────────────────────────
echo "============================================================"
echo "  FOTA Local HTTP Server"
echo "============================================================"
echo "  Serving from : $RELEASE_DIR"
echo "  Local IP     : $LOCAL_IP"
echo "  Port         : $PORT"
echo ""
echo "  IMPORTANT — Set these two values BEFORE hardware test:"
echo ""
echo "  1. In release/manifest.json, set:"
echo "     \"url\": \"$LOCAL_IP/firmware-v<VERSION>-b<BUILD>.bin\""
echo "     \"port\": $PORT"
echo ""
echo "  2. In include/app_config.h, set:"
echo "     #define FOTA_MANIFEST_URL  \"$LOCAL_IP\""
echo "     #define FOTA_MANIFEST_PORT  $PORT"
echo "     #define FOTA_MANIFEST_PATH  \"/manifest.json\""
echo ""
echo "  Files available:"
ls -lh "$RELEASE_DIR" 2>/dev/null || echo "  (empty)"
echo ""
echo "  Server starting on http://$LOCAL_IP:$PORT"
echo "  Press Ctrl+C to stop."
echo "============================================================"
echo ""

# ── Start HTTP server ─────────────────────────────────────────────────────────
cd "$RELEASE_DIR"
python3 -m http.server "$PORT"
