CC ?= gcc
AS ?= as
LD ?= ld
PYTHON ?= python3
CFLAGS = -m32 -march=i686 -Os -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables -fno-builtin -fno-common -Wall -Wextra -std=c11
LDFLAGS = -m elf_i386 -nostdlib -T kernel/linker.ld
BUILD = build
.PHONY: all clean inspect efi verify
all: $(BUILD)/clawos-native-bios.img $(BUILD)/clawos-native-bios.iso
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/start.o: boot/start.S | $(BUILD)
	$(AS) --32 $< -o $@
$(BUILD)/kernel.o: kernel/kernel.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/kernel.elf: $(BUILD)/start.o $(BUILD)/kernel.o kernel/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(BUILD)/start.o $(BUILD)/kernel.o
$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	objcopy -O binary $< $@
$(BUILD)/boot.o: boot/boot.S $(BUILD)/kernel.bin
	$(AS) --32 --defsym KERNEL_SECTORS=$$($(PYTHON) -c "import os; print((os.stat('$(BUILD)/kernel.bin').st_size+511)//512)") $< -o $@
$(BUILD)/boot.bin: $(BUILD)/boot.o
	$(LD) -m elf_i386 -e _boot -Ttext 0x7c00 --oformat binary -nostdlib -o $@ $<
$(BUILD)/clawos-native-bios.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	$(PYTHON) tools/make_image.py $(BUILD)/boot.bin $(BUILD)/kernel.bin $@
$(BUILD)/clawos-native-bios.iso: $(BUILD)/clawos-native-bios.img
	$(PYTHON) tools/make_iso.py $< $@
efi:
	sh tools/build_efi.sh
verify: all efi
	$(PYTHON) tools/verify.py
inspect:
	file $(BUILD)/*
clean:
	rm -f $(BUILD)/*
