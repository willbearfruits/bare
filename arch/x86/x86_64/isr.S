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
        pushq $0
    .endif
    pushq $i
    jmp isr_common
    .set i, i + 1
.endr

isr_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, %rdi
    cld
    call isr_dispatch
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax
    addq $16, %rsp
    iretq

.section .note.GNU-stack,"",@progbits
