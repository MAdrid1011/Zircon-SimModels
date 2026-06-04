ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
}

SECTIONS
{
    . = ORIGIN(RAM);

    .text : ALIGN(4)
    {
        KEEP(*(.text.start))
        *(.text)
        *(.text.*)
    } > RAM

    .rodata : ALIGN(4)
    {
        *(.srodata)
        *(.srodata.*)
        *(.rodata)
        *(.rodata.*)
    } > RAM

    .data : ALIGN(4)
    {
        *(.sdata)
        *(.sdata.*)
        *(.data)
        *(.data.*)
    } > RAM

    .bss (NOLOAD) : ALIGN(4)
    {
        *(.sbss)
        *(.sbss.*)
        *(.bss)
        *(.bss.*)
        *(COMMON)
    } > RAM

    . = ORIGIN(RAM) + LENGTH(RAM);
    __stack_top = .;

    /DISCARD/ :
    {
        *(.comment)
        *(.eh_frame)
        *(.eh_frame.*)
    }
}
