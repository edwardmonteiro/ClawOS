#!/bin/sh
# ClawOS Build Script
# Builds all components from source
#
# SPDX-License-Identifier: Apache-2.0

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

CC="${CC:-gcc}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

echo "=== ClawOS Build ==="
echo "Root: $ROOT_DIR"
echo "CC:   $CC"
echo "Jobs: $JOBS"
echo ""

cd "$ROOT_DIR"

echo ">> Building kernel daemon (clawd)..."
make -C kernel -j"$JOBS" CC="$CC"

echo ">> Building init system (claw-init)..."
make -C init -j"$JOBS" CC="$CC"

echo ">> Building message bus (claw-bus)..."
make -C bus -j"$JOBS" CC="$CC"

echo ">> Building OpenClaw runtime..."
make -C openclaw -j"$JOBS" CC="$CC"

echo ">> Building CLI (claw)..."
make -C cli -j"$JOBS" CC="$CC"

echo ">> Building extensions..."
make -C extensions -j"$JOBS" CC="$CC"

echo ""
echo "=== Build Complete ==="
echo "Binaries:"
echo "  kernel/clawd"
echo "  init/claw-init"
echo "  bus/claw-bus"
echo "  openclaw/openclaw-runtime"
echo "  cli/claw"
echo "  extensions/sdk/examples/hello/hello.so"
