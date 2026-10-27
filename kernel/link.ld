/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* 본체는 0x100000에 평면 바이너리로 적재된다 */
ENTRY(_start)
SECTIONS
{
    . = 0x100000;
    .text : { *(.text.start) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : {
        __bss_start = .;
        *(.bss .bss.*) *(COMMON)
        __bss_end = .;
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
