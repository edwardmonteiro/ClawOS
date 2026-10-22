# Old Intel Mac mini: compatibility and safe test plan

**Target:** Intel Mac mini, approximately 2006-2011, with 4 GB RAM. The exact model identifier determines CPU instruction support, EFI bitness and potential BIOS/CSM compatibility.

## Find hardware details

In macOS Terminal, run these read-only commands:

    system_profiler SPHardwareDataType
    sysctl -n machdep.cpu.brand_string
    diskutil list

Keep the Model Identifier and CPU type, but do not share a serial number.

## Safe order of operations

1. Back up user files. Do not remove the existing OS.
2. On a development computer: build from source and run make verify.
3. Boot the custom BIOS floppy image in QEMU and test keyboard commands.
4. On the Mac, test the **EFI diagnostic image** only from a removable USB, if the firmware recognizes it.
5. Only if BIOS/CSM is explicitly available, try the legacy ISO on non-destructive media.
6. Report actual boot output and hardware identifiers.
7. Develop the appropriate native EFI kernel loader and drivers for the model.

## Important limitations

- Current EFI binaries **do not load the ClawOS kernel**.
- BIOS El Torito floppy emulation might not be supported for USB boot on these Macs.
- The current kernel uses VGA memory and PS/2 keyboard polling; native Apple EFI hardware may need GOP/framebuffer and USB drivers.
- The CPU could be 32-bit even when the device has 4 GB physical RAM.
- The boot files are diagnostics/prototypes, **not a disk installer**.

Do not use unverified physical device targets with disk imaging commands. Never overwrite the Mac EFI partition or the internal disk while the boot paths are untested.
