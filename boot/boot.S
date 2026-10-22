/* ClawOS BIOS boot sector (x86 16-bit). Pure first-party boot code.
 * Boot medium: 1.44MB floppy image (also El Torito floppy-emulation ISO).
 * Load kernel from sequential 512-byte sectors starting at LBA 1 to 0x10000.
 */
.code16
.section .text
.global _boot
_boot:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %ss
    movw $0x7c00, %sp
    movb %dl, boot_drive
    movw $0x1000, dest_segment
    movw $1, current_lba
load_loop:
    movw current_lba, %ax
    cmpw $KERNEL_SECTORS+1, %ax
    jae boot_complete
    /* floppy geometry: 18 sectors per track, 2 heads */
    xorw %dx, %dx
    movw $18, %bx
    divw %bx
    movb %dl, %cl
    incb %cl
    xorw %dx, %dx
    movw $2, %bx
    divw %bx
    movb %al, %ch
    movb %dl, %dh
    movw dest_segment, %ax
    movw %ax, %es
    xorw %bx, %bx
    movb boot_drive, %dl
    movb $0x02, %ah
    movb $0x01, %al
    int $0x13
    jc disk_error
    addw $0x20, dest_segment
    incw current_lba
    jmp load_loop
boot_complete:
    movb boot_drive, %dl
    ljmp $0x1000, $0x0000
disk_error:
    movw $error_text, %si
1:
    lodsb
    testb %al,%al
    jz 2f
    movb $0x0e,%ah
    movw $0x0007,%bx
    int $0x10
    jmp 1b
2:
    cli
    hlt
    jmp 2b
error_text: .asciz "ClawOS disk read error.\r\n"
boot_drive: .byte 0
current_lba: .word 0
dest_segment: .word 0
.org 510
.word 0xaa55
