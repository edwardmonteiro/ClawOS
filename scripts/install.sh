#!/bin/sh
# ClawOS Install Script
# Installs ClawOS to the specified destination
#
# SPDX-License-Identifier: Apache-2.0

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
DESTDIR="${DESTDIR:-}"
PREFIX="${PREFIX:-/usr/local}"

echo "=== ClawOS Install ==="
echo "DESTDIR: ${DESTDIR:-(root)}"
echo "PREFIX:  $PREFIX"
echo ""

# Install binaries
install -D -m 0755 "$ROOT_DIR/kernel/clawd"           "$DESTDIR/usr/sbin/clawd"
install -D -m 0755 "$ROOT_DIR/init/claw-init"         "$DESTDIR/usr/sbin/claw-init"
install -D -m 0755 "$ROOT_DIR/bus/claw-bus"            "$DESTDIR/usr/sbin/claw-bus"
install -D -m 0755 "$ROOT_DIR/openclaw/openclaw-runtime" "$DESTDIR/usr/sbin/openclaw-runtime"
install -D -m 0755 "$ROOT_DIR/cli/claw"               "$DESTDIR/usr/bin/claw"

# Install configuration
install -D -m 0644 "$ROOT_DIR/config/clawd.conf"      "$DESTDIR/etc/claw/clawd.conf"
install -D -m 0644 "$ROOT_DIR/config/bus.conf"        "$DESTDIR/etc/claw/bus.conf"
install -D -m 0644 "$ROOT_DIR/config/openclaw.conf"   "$DESTDIR/etc/claw/openclaw.conf"

# Install extension SDK headers
install -D -m 0644 "$ROOT_DIR/extensions/sdk/include/claw_ext.h" \
    "$DESTDIR/usr/include/claw/claw_ext.h"
install -D -m 0644 "$ROOT_DIR/kernel/include/claw/types.h" \
    "$DESTDIR/usr/include/claw/types.h"
install -D -m 0644 "$ROOT_DIR/kernel/include/claw/kernel.h" \
    "$DESTDIR/usr/include/claw/kernel.h"
install -D -m 0644 "$ROOT_DIR/kernel/include/claw/ipc.h" \
    "$DESTDIR/usr/include/claw/ipc.h"
install -D -m 0644 "$ROOT_DIR/bus/include/claw/bus.h" \
    "$DESTDIR/usr/include/claw/bus.h"

# Install example extension
install -D -m 0644 "$ROOT_DIR/extensions/sdk/examples/hello/hello.so" \
    "$DESTDIR/usr/lib/claw/extensions/hello.so" 2>/dev/null || true

# Create required directories
mkdir -p "$DESTDIR/etc/claw/manifests"
mkdir -p "$DESTDIR/etc/claw/init.d"
mkdir -p "$DESTDIR/var/lib/claw/registry"
mkdir -p "$DESTDIR/var/log/claw"

# Install rootfs overlay
if [ -d "$ROOT_DIR/rootfs" ]; then
    cp -a "$ROOT_DIR/rootfs/"* "$DESTDIR/" 2>/dev/null || true
fi

echo "=== Install Complete ==="
