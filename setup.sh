#!/bin/bash
# setup.sh — Build and install (kernel module + userspace app)
# Run as root. Supports Secure Boot via MOK module signing.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Output directory for built artifacts
OUTPUT_DIR="/root/NTS_WORK"
mkdir -p "$OUTPUT_DIR"

# MOK signing key storage
MOK_DIR="/root/NTS_WORK/mok"
MOK_KEY="$MOK_DIR/signing_key.pem"
MOK_CERT="$MOK_DIR/signing_key_pub.der"
MOK_PEM="$MOK_DIR/signing_key_pub.pem"

# Device name from shared header (keep in sync with shared/memrw_ioctl.h)
DEV_NAME="hidraw_aux"

# Kernel build dir (for sign-file tool)
KBUILD="/usr/src/kernels/$(uname -r)"

echo "============================================="
echo "  Setup"
echo "============================================="

# ── 1. Install dependencies ──────────────────────────────────────
echo ""
echo "[1/8] Installing dependencies..."
dnf install -y \
    kernel-devel \
    kernel-headers \
    gcc \
    gcc-c++ \
    cmake \
    glfw-devel \
    mesa-libGL-devel \
    mesa-libGLU-devel \
    git \
    make \
    mokutil \
    openssl \
    keyutils

# ── 2. Clone Dear ImGui ──────────────────────────────────────────
echo ""
echo "[2/8] Cloning Dear ImGui..."
if [ -d "usermode/imgui" ]; then
    echo "  imgui/ already exists, pulling latest..."
    cd usermode/imgui && git pull && cd ../..
else
    git clone https://github.com/ocornut/imgui.git usermode/imgui
fi

# ── 3. Generate MOK signing key (if not present) ─────────────────
echo ""
echo "[3/8] Checking MOK signing key..."
mkdir -p "$MOK_DIR"

if [ -f "$MOK_KEY" ] && [ -f "$MOK_CERT" ]; then
    echo "  MOK key already exists at $MOK_DIR"
else
    echo "  Generating new MOK key pair..."
    openssl req -new -x509 \
        -newkey rsa:2048 \
        -keyout "$MOK_KEY" \
        -out "$MOK_PEM" \
        -nodes \
        -days 36500 \
        -subj "/CN=Fedora Secure Boot Signer/"

    # Convert PEM cert to DER for mokutil
    openssl x509 -in "$MOK_PEM" -outform DER -out "$MOK_CERT"

    chmod 600 "$MOK_KEY"
    echo "  MOK key pair generated."
fi

# ── 4. Enroll MOK key (if not already enrolled) ─────────────────
echo ""
echo "[4/8] Checking MOK enrollment..."
if mokutil --test-key "$MOK_CERT" 2>&1 | grep -q "is already enrolled"; then
    echo "  MOK key is already enrolled. ✓"
else
    echo "  MOK key is NOT enrolled. Enrolling now..."
    echo ""
    echo "  ╔══════════════════════════════════════════════════════════╗"
    echo "  ║  You will be asked to set a ONE-TIME password.          ║"
    echo "  ║  Remember this password! You will need it at next boot  ║"
    echo "  ║  when the MOK Manager screen appears.                   ║"
    echo "  ║                                                         ║"
    echo "  ║  At the MOK Manager:                                    ║"
    echo "  ║    1. Select 'Enroll MOK'                               ║"
    echo "  ║    2. Select 'Continue'                                  ║"
    echo "  ║    3. Enter the password you set here                   ║"
    echo "  ║    4. Select 'Reboot'                                   ║"
    echo "  ╚══════════════════════════════════════════════════════════╝"
    echo ""
    mokutil --import "$MOK_CERT"

    echo ""
    echo "  MOK key queued for enrollment."
    echo "  You MUST reboot and complete enrollment in MOK Manager,"
    echo "  then re-run this script."
    echo ""
    echo "  Run: reboot"
    exit 0
fi

# ── 5. Build kernel module ───────────────────────────────────────
echo ""
echo "[5/8] Building kernel module..."
cd driver
make clean || true
make
echo "  Kernel module built: memrw.ko"

# ── 6. Sign kernel module ────────────────────────────────────────
echo ""
echo "[6/8] Signing kernel module..."
SIGN_FILE="$KBUILD/scripts/sign-file"
if [ ! -x "$SIGN_FILE" ]; then
    SIGN_FILE=$(find /usr/src/kernels/ -name "sign-file" -type f 2>/dev/null | head -1)
fi

if [ -x "$SIGN_FILE" ]; then
    "$SIGN_FILE" sha256 "$MOK_KEY" "$MOK_PEM" "$OUTPUT_DIR/memrw.ko"
    echo "  Module signed successfully. ✓"
else
    echo "  ERROR: sign-file tool not found!"
    exit 1
fi

cd "$SCRIPT_DIR"

# ── 7. Load kernel module ────────────────────────────────────────
echo ""
echo "[7/8] Loading kernel module..."

# Try to unhide and remove a previously hidden module instance.
# We use IOCTL_UNHIDE_MODULE to re-expose it, then rmmod.
MAJOR=$(grep " ${DEV_NAME}$" /proc/devices 2>/dev/null | head -1 | awk '{print $1}')
if [ -n "$MAJOR" ]; then
    echo "  Found existing module (major $MAJOR), unhiding for removal..."
    TMP_DEV="/dev/.hid_aux"
    if [ ! -c "$TMP_DEV" ]; then
        (umask 000; mknod "$TMP_DEV" c "$MAJOR" 0) 2>/dev/null || true
    fi
    if [ -c "$TMP_DEV" ]; then
        # Send IOCTL_UNHIDE_MODULE via python (simplest way without a C tool)
        python3 -c "
import fcntl, struct, os
fd = os.open('$TMP_DEV', os.O_RDWR)
# IOCTL_UNHIDE_MODULE = _IO('U', 204) = ((0) << 30) | (ord('U') << 8) | 204 = 0x55cc
fcntl.ioctl(fd, 0x55cc)
os.close(fd)
" 2>/dev/null || true
        rm -f "$TMP_DEV"
    fi
    sleep 0.3
    rmmod memrw 2>/dev/null || true
    sleep 0.5
fi

# Clean up any leftover device nodes
rm -f "/dev/${DEV_NAME}" 2>/dev/null || true
rm -f "/dev/.hid_aux" 2>/dev/null || true

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

# Create hidden device node for usermode app (avoids /proc access from usermode)
# NOTE: chmod/chcon won't work after driver loads because our newfstatat hook
#       returns ENOENT for .hid_aux. So we set permissions at creation via umask.
MAJOR=$(grep " ${DEV_NAME}$" /proc/devices 2>/dev/null | head -1 | awk '{print $1}')
if [ -n "$MAJOR" ]; then
    HIDDEN_NODE="/dev/.hid_aux"
    rm -f "$HIDDEN_NODE" 2>/dev/null || true
    (umask 000; mknod "$HIDDEN_NODE" c "$MAJOR" 0)
    echo "  Hidden device node created: $HIDDEN_NODE (major $MAJOR) ✓"
else
    echo "  WARNING: Could not find major number — hidden node not created!"
fi

# Clear dmesg to remove any traces
dmesg --clear 2>/dev/null || true

cd "$SCRIPT_DIR"

# ── 8. Build userspace application ───────────────────────────────
echo ""
echo "[8/8] Building userspace application..."
cd usermode
mkdir -p build
cd build
cmake ..
make -j"$(nproc)"
echo "  Built: gsd-housekeeping"

cd "$SCRIPT_DIR"

# ── Done ─────────────────────────────────────────────────────────
echo ""
echo "============================================="
echo "  Setup complete!"
echo ""
echo "  Build outputs in: $OUTPUT_DIR"
echo ""
echo "  To run:"
echo "    cd $OUTPUT_DIR"
echo "    sudo ./gsd-housekeeping"
echo ""
echo "  To unload kernel module:"
echo "    $SCRIPT_DIR/unload.sh"
echo "============================================="
