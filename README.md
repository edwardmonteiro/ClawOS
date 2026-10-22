# ClawOS Native

**Experimental agent-first operating system for old Intel Mac minis (2006-2011, 4 GB RAM).**

ClawOS Native is a **freestanding x86 kernel written from scratch in C and assembly**. The BIOS demo is **not Linux or macOS**. It is **not a complete operating system** and **does not yet run OpenClaw, Node.js or an LLM**.

**Status: v0.2 proof of concept.** Images have passed **structural verification only**. Neither QEMU boot nor physical Mac mini boot has been demonstrated. Do not erase your Mac disk.

## What is available

| Component | Status | Limitation |
|---|---|---|
| Custom BIOS boot sector | Compiled, structurally checked | Requires BIOS/CSM, not proven on Apple |
| x86 32-bit protected-mode kernel | Compiled | No paging, scheduler, or processes |
| VGA text shell and PS/2 keyboard polling | Implemented | Not boot or hardware-tested |
| Agent intent classification | Implemented | Deterministic keywords, not generative AI |
| In-RAM task approval queue | Implemented | Approve does NOT execute actions |
| Bootable-format floppy IMG and El Torito ISO | Built/verified | No verified successful boot |
| IA32 EFI and x64 EFI executables | Built/verified | **Diagnostic only**, not EFI kernel loader |
| 64 MiB EFI FAT32 USB image | Built/verified | Diagnostic only, not an OS installer |
| Disk filesystem, drivers, network, TLS | Not implemented | Needed for persistent agent tools |
| Upstream OpenClaw/Node runtime | Not implemented | Major runtime/POSIX compatibility required |

## Current architecture

    BIOS/CSM -> custom 16-bit stage 0 -> protected mode x86 kernel
             -> VGA terminal + PS/2 polling -> ClawOS shell
             -> keyword router -> request queue in RAM -> human approval (no execution)

Separate EFI32/EFI64 binaries test firmware entry only. They do **not** load the kernel.

## Build

On a Linux development machine, install GNU make, GCC with 32-bit freestanding compilation support, binutils, Python 3, Clang and lld-link. These are **host development tools**; ClawOS itself does not ship Linux.

    make
    make efi
    make verify

Outputs (ignored from Git, generated in build/):

    build/clawos-native-bios.img     - 1.44 MiB legacy BIOS floppy image
    build/clawos-native-bios.iso     - ~1.5 MiB El Torito BIOS ISO
    build/clawos-efi-probe-usb.img   - 64 MiB EFI32 + EFI64 diagnostic media
    build/BOOTIA32.EFI              - UEFI32 diagnostic
    build/BOOTX64.EFI               - UEFI64 diagnostic

The GitHub Actions workflow builds and performs structural checks. On success, the images are available as temporary workflow artifacts.

### Test in QEMU first

After building, test interactively on a computer running QEMU:

    qemu-system-i386 -m 512 -drive file=build/clawos-native-bios.img,if=floppy,format=raw

Or:

    qemu-system-i386 -m 512 -cdrom build/clawos-native-bios.iso -boot d

Try shell commands: help, about, hardware, status, skills, agent organizar arquivos, tasks, approve, reject.

The agent command **does not** modify files, access a model or execute arbitrary programs. Task requests disappear at reboot.

**Verification limits:** make verify checks BIOS signature and kernel payload, ISO/El Torito structures, FAT32 layout and EFI PE headers. It cannot prove boot compatibility.

## Old Mac mini support

Some 2006 Mac minis use 32-bit CPUs, while later Intel models have 64-bit CPUs. Apple EFI firmware may be 32- or 64-bit and may lack usable BIOS compatibility boot for custom USB images.

| Boot mode | Image | Support |
|---|---|---|
| BIOS/CSM legacy | BIOS ISO or floppy IMG | Candidate, unverified on Mac |
| IA32 EFI | FAT32 USB diagnostic IMG | Boot probe only, unverified on Mac |
| x64 EFI | FAT32 USB diagnostic IMG | Boot probe only, unverified on Mac |

**Do not format or replace the internal drive.** Keep macOS bootable and identify the exact model first. Use a removable medium only after safely confirming the boot mode.

See [Mac mini test plan](docs/MAC-MINI.md) and [architecture/roadmap](docs/ARCHITECTURE.md).

## Roadmap

1. QEMU execution with captured serial or VGA evidence.
2. Mac-specific EFI firmware diagnosis; implement EFI32/x64 kernel loader.
3. Memory map, paging, interrupts, timer and hardware drivers.
4. Filesystem, persistent state, isolated processes, network and TLS.
5. Secure native AI inference transport and executable agent capabilities.
6. Evaluate OpenClaw-compatible gateway protocol or Node/POSIX compatibility.

The target is **minimal memory use with cloud-hosted model inference**. No measured RAM or boot-time claims are made yet.

## Repository layout

    boot/               original BIOS boot sector and mode transition
    kernel/             freestanding kernel and linker script
    uefi/               EFI firmware diagnostic code
    tools/              image generation and structural verification
    docs/               compatibility and roadmap
    .github/workflows/  CI build and image artifacts
    Makefile            build definitions

## Previous Linux-based ClawOS

The former main branch was preserved at [archive/linux-agent-runtime-2026-10](../../tree/archive/linux-agent-runtime-2026-10). The new main is intentionally a different project: a true bare-metal kernel experiment rather than a Linux-based agent runtime.

## Security and license

All scripts write **files under build/**, not physical disks. Do not store API keys in Git or firmware images. The new source is experimental; license terms for the rewritten implementation should be reviewed before redistribution.
