#!/bin/bash
# unload.sh — Unhide and unload the kernel module
# The module hides itself from lsmod, so we need to send an IOCTL first.

set -e

DEV_NAME="uinput_helper"

MAJOR=$(grep " ${DEV_NAME}$" /proc/devices 2>/dev/null | awk '{print $1}')
if [ -z "$MAJOR" ]; then
    echo "Module not found in /proc/devices — already unloaded?"
    exit 0
fi

echo "Found module (major $MAJOR), sending unhide IOCTL..."
TMP_DEV="/tmp/.${DEV_NAME}_$$"
mknod "$TMP_DEV" c "$MAJOR" 0
python3 -c "
import fcntl, os
fd = os.open('$TMP_DEV', os.O_RDWR)
fcntl.ioctl(fd, 0x55cc)  # IOCTL_UNHIDE_MODULE
os.close(fd)
"
rm -f "$TMP_DEV"

echo "Module unhidden, running rmmod..."
rmmod memrw
echo "Module unloaded. ✓"
