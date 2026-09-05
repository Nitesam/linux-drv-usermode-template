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
PUB_KEY="$MOK_DIR/signing_key_pub.der"

PRIV_KEY=$(ls "$MOK_DIR"/*.priv "$MOK_DIR"/*.pem "$MOK_DIR"/*.key 2>/dev/null | head -n 1)

if [ -n "$PRIV_KEY" ] && [ -f "$PUB_KEY" ]; then
    echo "    Using private key: $PRIV_KEY"
    /usr/src/kernels/$(uname -r)/scripts/sign-file sha256 \
        "$PRIV_KEY" "$PUB_KEY" \
        "$OUTPUT_DIR/memrw.ko"
    echo "  ✓ Signed"
else
    echo "  ⚠ MOK keys not found in $MOK_DIR, skipping signing"
    exit 1
fi

echo "[3/4] Unloading old module..."
"$SCRIPT_DIR/unload.sh"

echo "[4/4] Loading module..."
insmod "$OUTPUT_DIR/memrw.ko"
echo "  ✓ Module loaded"

"$SCRIPT_DIR/create_device.sh"
echo "  Done. Memory operations require CAP_SYS_PTRACE."
