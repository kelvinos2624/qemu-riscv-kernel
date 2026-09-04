OUTPUT_ARCH(riscv)
ENTRY(_start)

USER_CODE_BASE = 0x1000;
PAGE_SIZE = 0x1000;

SECTIONS
{
    . = USER_CODE_BASE;

    .text : ALIGN(4) {
        KEEP(*(.text.user_start))
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
    }

    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
    }

    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.comment)
        *(.riscv.attributes)
        *(.eh_frame .eh_frame.*)
        *(.note .note.*)
    }

    ASSERT(SIZEOF(.text) <= PAGE_SIZE, "user text exceeds one mapped code page")
    ASSERT(SIZEOF(.rodata) == 0, "user rodata is not mapped")
    ASSERT(SIZEOF(.data) == 0, "user data is not mapped")
    ASSERT(SIZEOF(.bss) == 0, "user bss is not mapped")
}
