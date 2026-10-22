#!/bin/sh
set -eu
mkdir -p build
for arch in x86_64 i686; do
  if [ "$arch" = x86_64 ]; then machine=x64;dest=BOOTX64; else machine=x86;dest=BOOTIA32;fi
  clang -target "$arch"-pc-win32 -ffreestanding -fno-stack-protector -fshort-wchar -O2 -fno-builtin -Wno-ignored-attributes -c uefi/probe.c -o "build/$dest.obj"
  lld-link "/machine:$machine" /subsystem:efi_application /entry:efi_main /nodefaultlib /opt:ref "/out:build/$dest.EFI" "build/$dest.obj"
done
python3 tools/make_fat32.py build/BOOTX64.EFI build/BOOTIA32.EFI build/clawos-efi-probe-usb.img
