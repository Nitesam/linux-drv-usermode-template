#!/bin/bash
# build_usermode.sh — Quick rebuild of usermode only
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR/usermode"
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -2
make -j"$(nproc)"
echo ""
echo "  ✓ Built: /root/NTS_WORK/gsd-housekeeping"
echo "  Run: cd /root/NTS_WORK && sudo ./gsd-housekeeping"
