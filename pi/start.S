// Raspberry Pi 3（aarch64）の最初のコード。コア 0 だけ動かし、残りは止める
    .section .text.boot
    .globl _start
_start:
    mrs x0, mpidr_el1
    and x0, x0, #3
    cbnz x0, park
    ldr x0, =_start             // スタックは 0x80000 から下へ伸ばす
    mov sp, x0
    ldr x0, =__bss_start
    ldr x1, =__bss_end
1:  cmp x0, x1
    b.hs 2f
    str xzr, [x0], #8
    b 1b
2:  bl main
park:
    wfe
    b park
