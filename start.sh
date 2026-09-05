#!/bin/bash
# start.sh — Load the pre-built kernel module and launch the userspace app.
# Run as root.  Does NOT rebuild anything.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="/root/NTS_WORK"
MOK_DIR="$OUTPUT_DIR/mok"
MOK_CERT="$MOK_DIR/signing_key_pub.der"
DEV_NAME="hidraw_aux"

# ── Sanity checks ────────────────────────────────────────────────
if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: run as root."
    exit 1
fi

if [ ! -f "$OUTPUT_DIR/memrw.ko" ]; then
    echo "ERROR: $OUTPUT_DIR/memrw.ko not found."
    echo "       Run setup.sh first to build the driver."
    exit 1
fi

echo "============================================="
echo "  Quick Start (no rebuild)"
echo "============================================="

# ── 1. Unload previous instance (if any) ─────────────────────────
echo ""
echo "[1/2] Unloading previous module instance..."

"$SCRIPT_DIR/unload.sh"

echo "  Done. ✓"

# ── 2. Load kernel module ────────────────────────────────────────
echo ""
echo "[2/2] Loading kernel module..."

if insmod "$OUTPUT_DIR/memrw.ko"; then
    echo "  Module loaded. ✓"
else
    echo ""
    echo "  ERROR: insmod failed."
    echo "  Possible causes:"
    echo "    - MOK key not enrolled (reboot after mokutil --import)"
    echo "    - Previous hidden module still loaded (reboot required)"
    echo ""
    echo "  Check: mokutil --test-key $MOK_CERT"
    exit 1
fi

"$SCRIPT_DIR/create_device.sh"
