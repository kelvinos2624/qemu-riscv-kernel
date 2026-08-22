OUTPUT_ARCH(riscv)
ENTRY(_start)

KERNEL_BASE = 0x80000000;

SECTIONS
{
    . = KERNEL_BASE;

    PROVIDE(__kernel_start = .);

    .text : ALIGN(4K) {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    .rodata : ALIGN(4K) {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4K) {
        *(.data .data.*)
    }

    .bss : ALIGN(4K) {
        PROVIDE(__bss_start = .);
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        PROVIDE(__bss_end = .);
    }

    . = ALIGN(16);
    PROVIDE(__stack_bottom = .);
    . += 16K;
    PROVIDE(__stack_top = .);

    . = ALIGN(4K);
    PROVIDE(__kernel_end = .);
}
