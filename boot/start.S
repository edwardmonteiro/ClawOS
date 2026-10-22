/* Real mode to 32-bit protected mode entry. Loaded at physical 0x10000.
 * BIOS stage0 loads this image first, then jumps to 1000:0000.
 */
.section .text.boot, "ax", @progbits
.code16
.global _start
_start:
    cli
    movw $0x1000, %ax
    movw %ax, %ds
    movw %ax, %es
    movw $0x7000, %ax
    movw %ax, %ss
    movw $0xfff0, %sp
    /* enable A20 through fast A20 gate */
    inb $0x92, %al
    orb $0x02, %al
    outb %al, $0x92
    /* CS/DS = 0x1000: refer to descriptor by offset from _start. */
    lgdt gdt_descriptor - _start
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmpl $0x08, $protected_entry
.align 8
gdt:
    .quad 0
    /* 0-4GiB flat code segment, 32-bit protected mode */
    .word 0xffff, 0x0000
    .byte 0x00, 0x9a, 0xcf, 0x00
    /* 0-4GiB flat data segment */
    .word 0xffff, 0x0000
    .byte 0x00, 0x92, 0xcf, 0x00
gdt_end:
gdt_descriptor:
    .word gdt_end - gdt - 1
    .long gdt
.code32
protected_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x90000, %esp
    cld
    /* C requires zero-initialized globals. */
    movl $__bss_start, %edi
    movl $__bss_end, %ecx
    subl %edi, %ecx
    xorl %eax, %eax
    rep stosb
    call kernel_main
1:  cli
    hlt
    jmp 1b
