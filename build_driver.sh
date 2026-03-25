#!/bin/bash
# build_driver.sh — Quick rebuild, sign, and reload kernel module
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="/root/NTS_WORK"

echo "[1/4] Building kernel module..."
cd "$SCRIPT_DIR/driver"
make -C /lib/modules/$(uname -r)/build M="$SCRIPT_DIR/driver" clean
make -C /lib/modules/$(uname -r)/build M="$SCRIPT_DIR/driver" modules
cp -f "$SCRIPT_DIR/driver/memrw.ko" "$OUTPUT_DIR/memrw.ko"
echo "  ✓ Built memrw.ko"

echo "[2/4] Signing..."
MOK_DIR="$OUTPUT_DIR/mok"
if [ -f "$MOK_DIR/signing_key.priv" ] && [ -f "$MOK_DIR/signing_key_pub.der" ]; then
    /usr/src/kernels/$(uname -r)/scripts/sign-file sha256 \
        "$MOK_DIR/signing_key.priv" "$MOK_DIR/signing_key_pub.der" \
        "$OUTPUT_DIR/memrw.ko"
    echo "  ✓ Signed"
else
    echo "  ⚠ MOK keys not found, skipping signing"
fi

echo "[3/4] Unloading old module..."
"$SCRIPT_DIR/unload.sh" 2>/dev/null || true

echo "[4/4] Loading module..."
insmod "$OUTPUT_DIR/memrw.ko"
echo "  ✓ Module loaded"
echo ""
echo "  Done! Run: cd /root/NTS_WORK && sudo ./gsd-housekeeping"
