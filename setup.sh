#!/bin/bash
# setup.sh — Build and install (kernel module + userspace app)
# Run as root. Supports Secure Boot via MOK module signing.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ "$(id -u)" -ne 0 ]; then
    echo "ERROR: setup.sh must run as root."
    echo "Run: sudo $SCRIPT_DIR/setup.sh"
    exit 1
fi

# Output directory for built artifacts
OUTPUT_DIR="${OUTPUT_DIR:-/root/NTS_WORK}"
mkdir -p "$OUTPUT_DIR"

# MOK signing key storage
MOK_DIR="${MOK_DIR:-$OUTPUT_DIR/mok}"
MOK_KEY="$MOK_DIR/signing_key.pem"
MOK_CERT="$MOK_DIR/signing_key_pub.der"
MOK_PEM="$MOK_DIR/signing_key_pub.pem"

# Device name from shared header (keep in sync with shared/memrw_ioctl.h)
DEV_NAME="hidraw_aux"

# Kernel build dir must match the currently running kernel.
RUNNING_KERNEL="$(uname -r)"
KERNEL_ARCH="$(uname -m)"
KERNEL_EVR="${RUNNING_KERNEL%.$KERNEL_ARCH}"
KBUILD="/lib/modules/${RUNNING_KERNEL}/build"
DNF=(dnf --refresh)

echo "============================================="
echo "  Setup"
echo "============================================="

# ── 1. Install dependencies ──────────────────────────────────────
echo ""
echo "[1/8] Installing dependencies..."
if ! "${DNF[@]}" install -y \
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
    keyutils; then
    echo ""
    echo "  ERROR: Could not install required dependencies."
    exit 1
fi

if [ ! -d "$KBUILD" ]; then
    echo "  Kernel build directory is missing for ${RUNNING_KERNEL}."
    echo "  Installing matching kernel-devel package..."
    if ! "${DNF[@]}" install -y "kernel-devel = ${KERNEL_EVR}"; then
        KERNEL_VERSION="${KERNEL_EVR%%-*}"
        KERNEL_RELEASE="${KERNEL_EVR#*-}"
        KOJI_KERNEL_DEVEL_URL="https://kojipkgs.fedoraproject.org/packages/kernel/${KERNEL_VERSION}/${KERNEL_RELEASE}/${KERNEL_ARCH}/kernel-devel-${KERNEL_EVR}.${KERNEL_ARCH}.rpm"

        echo ""
        echo "  Trying Fedora Koji archive for the exact running kernel-devel:"
        echo "    $KOJI_KERNEL_DEVEL_URL"
        if ! "${DNF[@]}" install -y "$KOJI_KERNEL_DEVEL_URL"; then
            LATEST_KERNEL_EVR="$("${DNF[@]}" repoquery --available --latest-limit=1 --qf '%{evr}' kernel-devel | tail -n 1)"

            if [ -n "$LATEST_KERNEL_EVR" ] && [ "$LATEST_KERNEL_EVR" != "$KERNEL_EVR" ]; then
                echo ""
                echo "  Fedora no longer provides kernel-devel for the running kernel:"
                echo "    running kernel: ${RUNNING_KERNEL}"
                echo "    needed devel:   kernel-devel = ${KERNEL_EVR}"
                echo ""
                echo "  Installing the newest matching kernel/kernel-devel instead:"
                echo "    ${LATEST_KERNEL_EVR}.${KERNEL_ARCH}"
                if ! "${DNF[@]}" install -y \
                    "kernel = ${LATEST_KERNEL_EVR}" \
                    "kernel-core = ${LATEST_KERNEL_EVR}" \
                    "kernel-modules = ${LATEST_KERNEL_EVR}" \
                    "kernel-modules-core = ${LATEST_KERNEL_EVR}" \
                    "kernel-devel = ${LATEST_KERNEL_EVR}"; then
                    echo ""
                    echo "  ERROR: Could not install the newest matching kernel packages."
                    exit 1
                fi

                echo ""
                echo "  Reboot required before continuing."
                echo "  Boot into kernel ${LATEST_KERNEL_EVR}.${KERNEL_ARCH}, then run setup.sh again."
                exit 0
            fi

            echo ""
            echo "  ERROR: Could not install kernel-devel for the running kernel."
            echo "  Running kernel: ${RUNNING_KERNEL}"
            echo "  Needed package: kernel-devel = ${KERNEL_EVR}"
            exit 1
        fi
    fi
fi

if [ ! -d "$KBUILD" ]; then
    echo ""
    echo "  ERROR: Kernel build directory not found: $KBUILD"
    echo "  Running kernel: ${RUNNING_KERNEL}"
    echo ""
    echo "  Installed kernel-devel trees:"
    find /usr/src/kernels -mindepth 1 -maxdepth 1 -type d -printf "    - %f\n" 2>/dev/null || true
    echo ""
    echo "  Install the matching package:"
    echo "    dnf install -y 'kernel-devel = ${KERNEL_EVR}'"
    echo ""
    echo "  Or reboot into a kernel that has a matching kernel-devel package."
    exit 1
fi

# ── 2. Clone Dear ImGui ──────────────────────────────────────────
echo ""
echo "[2/8] Cloning Dear ImGui..."
IMGUI_DIR="usermode/imgui"
if [ -d "$IMGUI_DIR" ]; then
    echo "  imgui/ already exists, pulling latest..."
    if ! git -C "$IMGUI_DIR" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        echo "  ERROR: $IMGUI_DIR exists but is not a git repository."
        exit 1
    fi

    # Some shared/mounted filesystems mark every file executable. Ignore those
    # permission-only changes so they do not block updating the vendored copy.
    git -C "$IMGUI_DIR" config core.filemode false

    if ! git -C "$IMGUI_DIR" diff --quiet || ! git -C "$IMGUI_DIR" diff --cached --quiet; then
        echo "  ERROR: $IMGUI_DIR has local content changes."
        echo "  Commit or stash them before running setup.sh."
        exit 1
    fi

    git -C "$IMGUI_DIR" pull --ff-only
else
    git clone https://github.com/ocornut/imgui.git "$IMGUI_DIR"
    git -C "$IMGUI_DIR" config core.filemode false
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
make KDIR="$KBUILD" clean || true
make KDIR="$KBUILD"
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

"$SCRIPT_DIR/unload.sh"

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

"$SCRIPT_DIR/create_device.sh"

cd "$SCRIPT_DIR"

# ── 8. Build userspace application ───────────────────────────────
echo ""
echo "[8/8] Building userspace application..."
cd usermode
mkdir -p build
cd build
cmake -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$OUTPUT_DIR" ..
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
