#!/bin/bash
# Unload the hidden module after clients have closed their descriptors.
# A hidden module is not on the module list: unhide it via the ioctl first.
set -euo pipefail
if [ -c /dev/.hid_aux ]; then
    python3 - <<'EOF' 2>/dev/null || true
import fcntl, os
fd = os.open('/dev/.hid_aux', os.O_RDWR)
fcntl.ioctl(fd, 0x55cc)  # IOCTL_UNHIDE_MODULE = _IO('U', 204)
os.close(fd)
EOF
    sleep 0.3
fi
rmmod memrw 2>/dev/null || true
rm -f /dev/.hid_aux
rm -f /dev/uinput_helper
