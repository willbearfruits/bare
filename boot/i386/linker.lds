OUTPUT_FORMAT(elf32-i386)
ENTRY(_start)
SECTIONS {
    . = 0x100000;
    _kernel_start = .;
    .multiboot : { KEEP(*(.multiboot)) }
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    doom_data : { *(doom_data) }          /* Doom's variables: kept at boot, put back for each fresh start (core/doomhost.c) */
    .bss : { *(.bss .bss.*) *(COMMON) }
    doom_bss : { *(doom_bss) }
    _kernel_end = .;
    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) }
}
