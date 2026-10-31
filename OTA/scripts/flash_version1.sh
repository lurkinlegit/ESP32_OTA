#!/bin/bash
# =============================================================================
# flash_version1.sh — Flash Version 1 (factory baseline) to the board via USB
# =============================================================================
# Usage:   ./scripts/flash_version1.sh [port]
# Example: ./scripts/flash_version1.sh              (auto-detect port)
#          ./scripts/flash_version1.sh /dev/ttyUSB0 (explicit port)
#
# What it does:
#   1. Checks that CURRENT_FIRMWARE_BUILD is 1 in src/fota_engine.h
#   2. Checks that FOTA_MANIFEST_URL is NOT the placeholder
#   3. Builds and flashes firmware to the board via USB
#   4. Opens the serial monitor at 115200 baud
#
# BEFORE running this script:
#   - Update FOTA_MANIFEST_URL in include/app_config.h to your laptop IP
#   - Confirm CURRENT_FIRMWARE_BUILD is 1 in src/fota_engine.h
#   - Make sure the board is connected via USB
# =============================================================================

set -e

# Ensure PlatformIO CLI is in PATH
export PATH="$HOME/.platformio/penv/bin:$PATH"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
FOTA_ENGINE_H="$PROJECT_DIR/src/fota_engine.h"
APP_CONFIG_H="$PROJECT_DIR/include/app_config.h"
SERIAL_PORT="${1:-}"  # Optional: explicit serial port

echo "============================================================"
echo "  ESP32 FOTA — Flash Version 1 (Factory Baseline)"
echo "============================================================"
echo ""

# ── Pre-flight Check 1: Build number must be 1 ───────────────────────────────
echo "[CHECK 1/3] Verifying CURRENT_FIRMWARE_BUILD = 1..."
CURRENT_BUILD=$(grep '#define CURRENT_FIRMWARE_BUILD' "$FOTA_ENGINE_H" \
               | awk '{print $3}')
if [ "$CURRENT_BUILD" != "1" ]; then
    echo ""
    echo "  FAIL: CURRENT_FIRMWARE_BUILD is $CURRENT_BUILD, expected 1."
    echo ""
    echo "  This script is for flashing the initial baseline firmware."
    echo "  To fix: open src/fota_engine.h and set:"
    echo "    #define CURRENT_FIRMWARE_BUILD    1"
    echo "    #define CURRENT_FIRMWARE_VERSION  \"1.0.0\""
    echo ""
    exit 1
fi
echo "         OK (build = $CURRENT_BUILD)"
echo ""

# ── Pre-flight Check 2: Manifest URL must not be the placeholder ──────────────
echo "[CHECK 2/3] Verifying FOTA_MANIFEST_URL is configured..."
MANIFEST_URL=$(grep '#define FOTA_MANIFEST_URL' "$APP_CONFIG_H" \
              | sed 's/.*"\(.*\)".*/\1/')
if [ "$MANIFEST_URL" = "PENDING_SENIOR_APPROVAL" ]; then
    echo ""
    echo "  FAIL: FOTA_MANIFEST_URL is still set to the placeholder."
    echo ""
    echo "  To fix: open include/app_config.h and set:"
    echo "    #define FOTA_MANIFEST_URL  \"<YOUR_LAPTOP_IP>\""
    echo "    (e.g. \"192.168.1.50\")"
    echo ""
    echo "  TIP: Run ./scripts/serve_release.sh to find your laptop IP."
    echo ""
    exit 1
fi
echo "         OK (URL = $MANIFEST_URL)"
echo ""

# ── Pre-flight Check 3: Board connected? ─────────────────────────────────────
echo "[CHECK 3/3] Checking for connected ESP32 board..."
DETECTED_PORT=""
for p in /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyACM0 /dev/ttyACM1; do
    if [ -e "$p" ]; then
        DETECTED_PORT="$p"
        break
    fi
done

if [ -n "$SERIAL_PORT" ]; then
    if [ ! -e "$SERIAL_PORT" ]; then
        echo "  FAIL: Specified port $SERIAL_PORT not found."
        exit 1
    fi
    echo "         Using specified port: $SERIAL_PORT"
elif [ -n "$DETECTED_PORT" ]; then
    SERIAL_PORT="$DETECTED_PORT"
    echo "         Auto-detected port: $SERIAL_PORT"
else
    echo "  WARNING: No serial port found. PlatformIO will try to auto-detect."
    echo "           If upload fails, connect the board and retry."
fi
echo ""

# ── Flash ─────────────────────────────────────────────────────────────────────
echo "============================================================"
echo "  Flashing Version 1.0.0 (Build 1) to board..."
echo "  Hold BOOT button on board if upload does not start."
echo "============================================================"
echo ""

cd "$PROJECT_DIR"

UPLOAD_FLAGS=""
if [ -n "$SERIAL_PORT" ]; then
    UPLOAD_FLAGS="--upload-port $SERIAL_PORT"
fi

pio run -e esp32dev --target upload $UPLOAD_FLAGS

echo ""
echo "============================================================"
echo "  Flash complete! Opening serial monitor..."
echo "  Press Ctrl+C to exit monitor."
echo "============================================================"
echo ""

# Open serial monitor
if [ -n "$SERIAL_PORT" ]; then
    pio device monitor -p "$SERIAL_PORT" -b 115200
else
    pio device monitor -b 115200
fi
