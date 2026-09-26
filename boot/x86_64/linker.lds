OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kmain)
PHDRS {
    limine_requests PT_LOAD;
    text PT_LOAD;
    rodata PT_LOAD;
    data PT_LOAD;
}
SECTIONS {
    . = 0xffffffff80000000;
    _kernel_start = .;
    .limine_requests : {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :limine_requests
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .text : { *(.text .text.*) } :text
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .rodata : { *(.rodata .rodata.*) } :rodata
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    .data : { *(.data .data.*) } :data
    doom_data : { *(doom_data) } :data    /* Doom's variables: kept at boot, put back for each fresh start (core/doomhost.c) */
    .bss : { *(.bss .bss.*) *(COMMON) } :data
    doom_bss : { *(doom_bss) } :data
    _kernel_end = .;
    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) }
}
