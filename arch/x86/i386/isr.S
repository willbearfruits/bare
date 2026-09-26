/* 256 fixed-size interrupt stubs; the IDT points at isr_stubs + 16*vector. */
.section .text
.global isr_stubs
.align 16
isr_stubs:
.set i, 0
.rept 256
    .align 16
    .if (i == 8) || (i >= 10 && i <= 14) || (i == 17) || (i == 21) || (i == 29) || (i == 30)
    .else
        pushl $0
    .endif
    pushl $i
    jmp isr_common
    .set i, i + 1
.endr

isr_common:
    pusha
    cld
    pushl %esp
    call isr_dispatch
    addl $4, %esp
    popa
    addl $8, %esp
    iret

.section .note.GNU-stack,"",@progbits
