/* Multiboot2 header + entry. Limine (BIOS or UEFI) drops us here in 32-bit protected mode, paging off. */
.section .multiboot, "a"
.align 8
mb2_header_start:
    .long 0xE85250D6                 /* magic */
    .long 0                          /* architecture: i386 */
    .long mb2_header_end - mb2_header_start
    .long -(0xE85250D6 + 0 + (mb2_header_end - mb2_header_start))
    /* framebuffer request */
    .align 8
    .short 5, 0
    .long 20
    .long 0, 0, 32
    /* end tag */
    .align 8
    .short 0, 0
    .long 8
mb2_header_end:

.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:

.section .text
.global _start
_start:
    cli
    movl $stack_top, %esp
    pushl %ebx                       /* multiboot2 info pointer */
    pushl %eax                       /* magic */
    call kmain
1:  hlt
    jmp 1b

.section .note.GNU-stack,"",@progbits
