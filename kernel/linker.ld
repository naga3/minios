/* カーネルは物理アドレス 1MB に置く（ページテーブルはそのままの番地で使う） */
ENTRY(_start)
SECTIONS {
    . = 1M;
    .text : { *(.multiboot) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    .bss : { *(.bss*) *(COMMON) }
    kernel_end = .;
}
