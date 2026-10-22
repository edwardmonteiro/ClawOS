# ClawOS Native — architecture and milestones

## v0.2 source scope

This is an original 32-bit freestanding x86 kernel, built from C and assembly, with a custom BIOS bootloader and optional UEFI boot **probes**. It is independent of Linux/macOS.

Current kernel features: 16-bit stage 0, protected-mode transition, flat memory descriptor table, VGA 80x25 shell, PS/2 keyboard polling, CPUID vendor display, keyword intent routing, volatile task queue with explicit approval.

**No scheduler, MMU paging manager, filesystem, drivers for native Mac USB/graphics, TLS, network, executable tasks, Node.js or OpenClaw.**

## Agent semantics

An agent request only classifies text by keywords and queues an in-memory task. Approving a request changes task state, **not the machine state**. There is no inference engine or model connection. This permits experimenting with agent UX before enabling actions.

## Planned native architecture

    EFI32/EFI64 / BIOS legacy hardware bootstrap
        -> native kernel: memory, paging, interrupts, scheduling
        -> drivers: display, USB, storage, NIC
        -> filesystem + networking + secure TLS
        -> isolated process/userland runtime
        -> permissioned tools + audit log + model gateway
        -> ClawOS agent shell and persistent task/memory store
        -> optional OpenClaw protocol bridge

## Technical challenges

1. Mac EFI32/x64 and BIOS fallback vary by model; the 2006-2011 range is not one boot target.
2. EFI native framebuffer and USB are separate from VGA BIOS legacy handling.
3. Real 4 GiB RAM requires accurate firmware memory maps and allocation of reserved regions.
4. Network drivers and TLS must be implemented or ported before remote model inference.
5. Upstream OpenClaw is Node.js-based; embedding it in a new kernel requires userland/POSIX compatibility, not just an API call.
6. Before any agent tools become executable, memory protection, process isolation and human approvals are mandatory.

## Milestone acceptance tests

P0: real QEMU boot, visible shell, keyboard works.
P1: IA32/x64 firmware probe on exact Mac mini; native EFI loader demonstrates kernel entry.
P2: reliable driver/interrupt and memory management.
P3: persisted, sandboxed tasks; safe network.
P4: model API, skills and verified human-in-the-loop execution.
P5: OpenClaw interoperability with evidence.

**Compiled is not boot-tested. Structurally verified is not hardware-tested.**
