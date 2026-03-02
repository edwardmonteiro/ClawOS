#!/bin/sh
# ClawOS Image Builder
#
# Creates a minimal bootable image using an Alpine Linux base
# with ClawOS layered on top. The result is a lightweight
# system (~50MB) ready for OpenClaw integration.
#
# Requirements: root, debootstrap or alpine-make-rootfs, qemu-img
#
# SPDX-License-Identifier: Apache-2.0

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

IMAGE_NAME="${IMAGE_NAME:-clawos.img}"
IMAGE_SIZE="${IMAGE_SIZE:-256M}"
WORK_DIR="${WORK_DIR:-/tmp/clawos-build}"
ALPINE_VERSION="${ALPINE_VERSION:-3.20}"
ARCH="${ARCH:-x86_64}"

echo "=== ClawOS Image Builder ==="
echo "Image:   $IMAGE_NAME"
echo "Size:    $IMAGE_SIZE"
echo "Base:    Alpine Linux $ALPINE_VERSION ($ARCH)"
echo ""

if [ "$(id -u)" -ne 0 ]; then
    echo "Error: must run as root"
    exit 1
fi

# Clean up on exit
cleanup() {
    set +e
    umount "$WORK_DIR/rootfs/proc" 2>/dev/null
    umount "$WORK_DIR/rootfs/sys" 2>/dev/null
    umount "$WORK_DIR/rootfs/dev" 2>/dev/null
    umount "$WORK_DIR/rootfs" 2>/dev/null
    losetup -d "$LOOP_DEV" 2>/dev/null
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

mkdir -p "$WORK_DIR/rootfs"

echo ">> Creating disk image..."
qemu-img create -f raw "$IMAGE_NAME" "$IMAGE_SIZE"

echo ">> Setting up partitions..."
# Create a single partition
parted -s "$IMAGE_NAME" mklabel msdos
parted -s "$IMAGE_NAME" mkpart primary ext4 1MiB 100%
parted -s "$IMAGE_NAME" set 1 boot on

echo ">> Creating filesystem..."
LOOP_DEV=$(losetup -f --show -P "$IMAGE_NAME")
mkfs.ext4 -L clawos "${LOOP_DEV}p1"
mount "${LOOP_DEV}p1" "$WORK_DIR/rootfs"

echo ">> Installing Alpine base..."
# Use alpine-make-rootfs or manual bootstrap
if command -v alpine-make-rootfs >/dev/null 2>&1; then
    alpine-make-rootfs "$WORK_DIR/rootfs" \
        --branch "v$ALPINE_VERSION" \
        --packages "busybox musl linux-lts"
else
    # Manual minimal install
    MIRROR="https://dl-cdn.alpinelinux.org/alpine/v$ALPINE_VERSION/main"
    mkdir -p "$WORK_DIR/rootfs"/{bin,sbin,usr/bin,usr/sbin,usr/lib,etc,proc,sys,dev,tmp,run,var/log}

    echo "Note: For full image build, install alpine-make-rootfs"
    echo "      Creating skeleton rootfs instead..."

    # Create minimal /etc
    echo "clawos" > "$WORK_DIR/rootfs/etc/hostname"
    echo "root:x:0:0:root:/root:/bin/sh" > "$WORK_DIR/rootfs/etc/passwd"
    echo "root:x:0:" > "$WORK_DIR/rootfs/etc/group"
    cat > "$WORK_DIR/rootfs/etc/os-release" << 'OSEOF'
NAME="ClawOS"
VERSION="0.1.0"
ID=clawos
PRETTY_NAME="ClawOS 0.1.0"
HOME_URL="https://github.com/edwardmonteiro/ClawOS"
OSEOF
fi

echo ">> Installing ClawOS..."
cd "$ROOT_DIR"
DESTDIR="$WORK_DIR/rootfs" sh scripts/install.sh

echo ">> Configuring init..."
# Set claw-init as the init system
ln -sf /usr/sbin/claw-init "$WORK_DIR/rootfs/sbin/init"

echo ">> Installing bootloader..."
if command -v extlinux >/dev/null 2>&1; then
    mkdir -p "$WORK_DIR/rootfs/boot/syslinux"
    cat > "$WORK_DIR/rootfs/boot/syslinux/syslinux.cfg" << 'BOOTEOF'
DEFAULT clawos
LABEL clawos
    LINUX /boot/vmlinuz
    APPEND root=/dev/sda1 rw quiet init=/sbin/init
BOOTEOF
    extlinux --install "$WORK_DIR/rootfs/boot/syslinux"
fi

echo ">> Cleanup..."
sync

echo ""
echo "=== Image Complete: $IMAGE_NAME ==="
echo ""
echo "Test with QEMU:"
echo "  qemu-system-x86_64 -m 512 -drive file=$IMAGE_NAME,format=raw -nographic"
echo ""
echo "Or convert to other formats:"
echo "  qemu-img convert -f raw -O qcow2 $IMAGE_NAME clawos.qcow2"
echo "  qemu-img convert -f raw -O vmdk $IMAGE_NAME clawos.vmdk"
