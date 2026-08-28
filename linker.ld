OUTPUT_ARCH(riscv)
ENTRY(_start)

KERNEL_BASE = 0x80000000;
PAGE_SIZE = 0x1000;
RAM_SIZE = 128M;
RAM_END = KERNEL_BASE + RAM_SIZE;

SECTIONS
{
    . = KERNEL_BASE;

    PROVIDE(__ram_start = KERNEL_BASE);
    PROVIDE(__ram_end = RAM_END);
    PROVIDE(__kernel_start = .);

    .text : ALIGN(4K) {
        PROVIDE(__text_start = .);
        KEEP(*(.text.boot))
        *(.text .text.*)
        PROVIDE(__text_end = .);
    }

    .rodata : ALIGN(4K) {
        PROVIDE(__rodata_start = .);
        *(.rodata .rodata.*)
        PROVIDE(__rodata_end = .);
    }

    .data : ALIGN(4K) {
        PROVIDE(__data_start = .);
        *(.data .data.*)
        PROVIDE(__data_end = .);
    }

    .bss : ALIGN(4K) {
        PROVIDE(__bss_start = .);
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
        PROVIDE(__page_bitmap_start = .);
        . += ((RAM_SIZE / PAGE_SIZE) + 7) / 8;
        PROVIDE(__page_bitmap_end = .);
        PROVIDE(__bss_end = .);
    }

    . = ALIGN(16);
    PROVIDE(__stack_bottom = .);
    . += 16K;
    PROVIDE(__stack_top = .);

    . = ALIGN(4K);
    PROVIDE(__kernel_end = .);

    ASSERT(__kernel_end <= __ram_end, "kernel image exceeds configured RAM")
}
