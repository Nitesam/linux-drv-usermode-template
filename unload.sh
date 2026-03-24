#!/bin/bash
# unload.sh — Unhide and unload the kernel module
# The module hides itself from lsmod, so we need to send an IOCTL first.

DEV_NAME="uinput_helper"
HIDDEN_NODE="/dev/.hid_aux"

MAJOR=$(grep " ${DEV_NAME}$" /proc/devices 2>/dev/null | head -1 | awk '{print $1}')
if [ -z "$MAJOR" ]; then
    echo "Module not found in /proc/devices — already unloaded?"
    rm -f "$HIDDEN_NODE" 2>/dev/null || true
    exit 0
fi

echo "Found module (major $MAJOR), sending unhide IOCTL..."

# Use existing hidden node, or create one if missing.
# Use umask so mknod sets 0666 directly (chmod would fail due to our stat hook).
if [ ! -c "$HIDDEN_NODE" ]; then
    (umask 000; mknod "$HIDDEN_NODE" c "$MAJOR" 0) 2>/dev/null || true
fi

python3 -c "
import fcntl, os
fd = os.open('$HIDDEN_NODE', os.O_RDWR)
fcntl.ioctl(fd, 0x55cc)  # IOCTL_UNHIDE_MODULE
os.close(fd)
"

echo "Module unhidden, running rmmod..."
rmmod memrw
rm -f "$HIDDEN_NODE" 2>/dev/null || true
echo "Module unloaded. ✓"
