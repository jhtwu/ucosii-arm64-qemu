ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 0x00200000
}

SECTIONS
{
    . = ORIGIN(RAM);

    .text : ALIGN(16)
    {
        KEEP(*(.text.boot))
        *(.text*)
        *(.rodata*)
    } > RAM

    . = ALIGN(16);

    .data : ALIGN(16)
    {
        *(.data*)
    } > RAM

    . = ALIGN(16);

    .bss (NOLOAD) : ALIGN(16)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    } > RAM

    . = ALIGN(4096);
    .mmutable (NOLOAD) : ALIGN(4096)
    {
        mmu_table_start = .;
        . = . + 0x10000;
        mmu_table_end = .;
    } > RAM

    . = ALIGN(16);
    _stack_top = ORIGIN(RAM) + LENGTH(RAM);

    ASSERT((_stack_top - mmu_table_end) >= 0x1000, "Insufficient stack space after MMU table")
}
