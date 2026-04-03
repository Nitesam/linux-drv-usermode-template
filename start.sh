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

MAJOR=$(grep " ${DEV_NAME}$" /proc/devices 2>/dev/null | head -1 | awk '{print $1}')
if [ -n "$MAJOR" ]; then
    echo "  Found existing module (major $MAJOR), unhiding for removal..."
    TMP_DEV="/dev/.hid_aux"
    if [ ! -c "$TMP_DEV" ]; then
        (umask 000; mknod "$TMP_DEV" c "$MAJOR" 0) 2>/dev/null || true
    fi
    if [ -c "$TMP_DEV" ]; then
        python3 -c "
import fcntl, struct, os
fd = os.open('$TMP_DEV', os.O_RDWR)
fcntl.ioctl(fd, 0x55cc)
os.close(fd)
" 2>/dev/null || true
        rm -f "$TMP_DEV"
    fi
    sleep 0.3
    rmmod memrw 2>/dev/null || true
    sleep 0.5
fi

rm -f "/dev/${DEV_NAME}" 2>/dev/null || true
rm -f "/dev/.hid_aux" 2>/dev/null || true

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

MAJOR=$(grep " ${DEV_NAME}$" /proc/devices 2>/dev/null | head -1 | awk '{print $1}')
if [ -n "$MAJOR" ]; then
    HIDDEN_NODE="/dev/.hid_aux"
    rm -f "$HIDDEN_NODE" 2>/dev/null || true
    (umask 000; mknod "$HIDDEN_NODE" c "$MAJOR" 0)
    echo "  Hidden device node created: $HIDDEN_NODE (major $MAJOR) ✓"
else
    echo "  WARNING: Could not find major number — hidden node not created!"
fi

dmesg --clear 2>/dev/null || true
