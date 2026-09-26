/* Real-mode trampoline: lets the 32-bit kernel call BIOS interrupts (int 13h disk I/O on the boot USB stick).
   Copied to TRAMP_BASE at init. Register block lives at REGS. Only possible without paging — i386 build only. */
.set TRAMP_BASE, 0x8000
.set REGS,       0x7800     /* struct bios_regs: eax ebx ecx edx esi edi (u32 x6), ds es flags (u16 x3), intno u8 */
.set SAVE_ESP,   0x7F00
.set SAVE_GDT,   0x7F10
.set SAVE_IDT,   0x7F20
.set RM_IDT,     0x7F30
.set RM_STACK,   0x7E00

.section .rodata
.global tramp_start, tramp_end
.align 16
tramp_start:
.code32
    pushal
    pushfl
    movl %esp, SAVE_ESP
    sgdt SAVE_GDT
    sidt SAVE_IDT
    movw $0x3FF, RM_IDT
    movl $0, RM_IDT + 2
    ljmp $0x18, $(TRAMP_BASE + (pm16 - tramp_start))
.code16
pm16:
    movw $0x20, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl %cr0, %eax
    andl $0xFFFFFFFE, %eax
    movl %eax, %cr0
    ljmp $0, $(TRAMP_BASE + (rm - tramp_start))
rm:
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $RM_STACK, %sp
    lidt RM_IDT
    /* patch the interrupt number into the int instruction */
    movb REGS + 30, %al
    movb %al, %cs:(TRAMP_BASE + (intinsn - tramp_start) + 1)
    movl REGS + 0, %eax
    movl REGS + 4, %ebx
    movl REGS + 8, %ecx
    movl REGS + 12, %edx
    movl REGS + 16, %esi
    movl REGS + 20, %edi
    movw REGS + 26, %es
    movw REGS + 24, %ds
    sti
intinsn:
    int $0x13
    cli
    movl %eax, %ss:REGS + 0
    movl %ebx, %ss:REGS + 4
    movl %ecx, %ss:REGS + 8
    movl %edx, %ss:REGS + 12
    movl %esi, %ss:REGS + 16
    movl %edi, %ss:REGS + 20
    pushfw
    popw %ax
    movw %ax, %ss:REGS + 28
    xorw %ax, %ax
    movw %ax, %ds
    lgdtl SAVE_GDT
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmpl $0x08, $(TRAMP_BASE + (pm32 - tramp_start))
.code32
pm32:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl SAVE_ESP, %esp
    lidt SAVE_IDT
    popfl
    popal
    ret
tramp_end:

.section .note.GNU-stack,"",@progbits
