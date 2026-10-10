/* layout_test's game, where IPL3 puts a game's first megabyte */
OUTPUT_FORMAT("elf32-tradbigmips")
OUTPUT_ARCH(mips)
ENTRY(_start)
SECTIONS
{
    . = 0x80000400;
    .text : { KEEP(*(.text.start)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.* .sdata .sdata.*) }
    . = ALIGN(8);
    __bss_start = .;
    .bss : { *(.sbss .sbss.* .scommon .bss .bss.* COMMON) }
    . = ALIGN(8);
    __bss_end = .;
    /DISCARD/ : { *(.reginfo .pdr .comment .gnu.attributes .note* .MIPS.abiflags .mdebug*) }
}
