#!/bin/bash
# Create the hidden device node used by the runtime. The cdev is registered
# as "hidraw_aux"; the dot node is masked from readdir by the module itself.
set -euo pipefail
if [ "$(id -u)" -ne 0 ]; then
    echo "Run create_device.sh as root." >&2
    exit 1
fi
device_owner="${MEMRW_DEVICE_OWNER:-${SUDO_USER:-root}}"
device_uid="$(id -u -- "$device_owner")"
device_gid="$(id -g -- "$device_owner")"
device_major="$(awk '$2 == "hidraw_aux" { print $1; exit }' /proc/devices)"
if [[ ! "$device_major" =~ ^[0-9]+$ ]]; then
    echo "The hidraw_aux character device is not registered." >&2
    exit 1
fi
rm -f /dev/.hid_aux
mknod -m 0600 /dev/.hid_aux c "$device_major" 0
chown "$device_uid:$device_gid" /dev/.hid_aux
echo "Created /dev/.hid_aux (0600, owner $device_owner)."
