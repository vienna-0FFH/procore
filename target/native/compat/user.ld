/* LLD-compatible linker script for the uCore user programs.
 *
 * The original script reserves old GNU ld STABS sections at 0x200000.
 * Current LLD treats that backwards location-counter move as an invalid
 * output-file layout when the input objects contain no STABS data.  User
 * objects are intentionally built without debug sections, so those sections
 * are unnecessary here.
 */

OUTPUT_FORMAT("elf32-i386", "elf32-i386", "elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS {
    . = 0x800020;

    .text : {
        *(.text .stub .text.* .gnu.linkonce.t.*)
    } :text

    PROVIDE(etext = .);

    .rodata : {
        *(.rodata .rodata.* .gnu.linkonce.r.*)
    } :text

    . = ALIGN(0x1000);

    .data : {
        *(.data)
    } :data

    PROVIDE(edata = .);

    .bss : {
        *(.bss)
    } :data

    PROVIDE(end = .);

    /DISCARD/ : {
        *(.eh_frame .note.GNU-stack .comment)
    }
}
