/* si_test's ROM, as IPL3 would leave it: at the N64's 0x8000_0400, what
 * follows the code and data cleared (the runtime clears the RDRAM). */
OUTPUT_FORMAT("elf32-tradbigmips")
ENTRY(_start)
SECTIONS
{
	. = 0x80000400;
	.text : { KEEP(*(.text.start)) *(.text .text.*) }
	.rodata : { *(.rodata .rodata.*) *(.MIPS.abiflags) }
	.data : { *(.data .data.*) *(.sdata .sdata.*) }
	. = ALIGN(16);
	si_rom_end = .;
	.bss (NOLOAD) : { *(.sbss .sbss.*) *(.scommon) *(.bss .bss.*) *(COMMON) }
	/DISCARD/ : { *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes) *(.mdebug*) }
}
