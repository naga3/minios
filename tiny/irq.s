# 割り込み: IDT（割り込みの入り口の表）、割り込みコントローラ（PIC）、タイマー（PIT、1 秒に 100 回）
#
# 割り込みが来ると、CPU は今の仕事を止めて IDT の入り口に飛び、iretq で元の仕事に戻る。
# 使うのは タイマー（IRQ0 → 32 番）、キーボード（IRQ1 → 33 番）、音（IRQ5 → 37 番。sb16.s が通す）。CPU の例外（0〜31 番）が起きたら、知らせて止まる
    .code64
    .text
    .global irq_init, ticks, sleep_ticks

irq_init:
    push %rax
    push %rbx
    push %rcx
    push %rdi
    mov $idt, %rdi                      # IDT の 48 個の入り口を作る（1 つ 16 バイト）
    xor %ecx, %ecx
1:  mov exception_stubs(,%rcx,8), %rax
    cmp $32, %ecx
    jb 2f
    mov $spurious, %rax
    cmp $32, %ecx
    jne 3f
    mov $timer_isr, %rax
3:  cmp $33, %ecx
    jne 4f
    mov $kbd_isr, %rax
4:  cmp $37, %ecx
    jne 2f
    mov $sb16_isr, %rax                 # IRQ5: Sound Blaster 16（sb16.s）
2:  mov %ax, (%rdi)                     # 入り口の番地の下位 16 ビット
    movw $0x08, 2(%rdi)                 # コードのセグメント（boot.s の GDT）
    movb $0, 4(%rdi)
    movb $0x8e, 5(%rdi)                 # 存在する、割り込みゲート
    shr $16, %rax
    mov %ax, 6(%rdi)                    # 番地の中ほど 16 ビット
    shr $16, %rax
    mov %eax, 8(%rdi)                   # 番地の上位 32 ビット
    movl $0, 12(%rdi)
    add $16, %rdi
    inc %ecx
    cmp $48, %ecx
    jne 1b
    lidt idt_ptr

    # PIC: 割り込みの番号を 32〜47 に移す（0〜31 は CPU の例外と重なるので）
    mov $0x11, %al
    out %al, $0x20
    out %al, $0xa0
    mov $32, %al
    out %al, $0x21
    mov $40, %al
    out %al, $0xa1
    mov $4, %al
    out %al, $0x21
    mov $2, %al
    out %al, $0xa1
    mov $1, %al
    out %al, $0x21
    out %al, $0xa1
    mov $0xfc, %al                      # 通すのは IRQ0（タイマー）と IRQ1（キーボード）だけ
    out %al, $0x21
    mov $0xff, %al
    out %al, $0xa1

    # PIT: 1 秒に 100 回（1193182 Hz ÷ 11932）
    mov $0x36, %al
    out %al, $0x43
    mov $11932 & 0xff, %al
    out %al, $0x40
    mov $11932 >> 8, %al
    out %al, $0x40
    sti
    pop %rdi
    pop %rcx
    pop %rbx
    pop %rax
    ret

# sleep_ticks: rdi 回（1 回 = 10ms）ぶん眠る。眠っている間は hlt で CPU を休める
sleep_ticks:
    push %rax
    mov ticks, %rax
    add %rdi, %rax
1:  hlt
    cmp %rax, ticks
    jb 1b
    pop %rax
    ret

timer_isr:
    push %rax
    incq ticks
    mov $0x20, %al                      # 割り込みの終わりを PIC に知らせる
    out %al, $0x20
    pop %rax
    iretq

kbd_isr:
    push %rax
    in $0x60, %al                       # 押された・離されたキーの番号（スキャンコード）
    call kbd_scancode
    mov $0x20, %al
    out %al, $0x20
    pop %rax
    iretq

spurious:                               # 使っていない割り込み
    push %rax
    mov $0x20, %al
    out %al, $0x20
    out %al, $0xa0
    pop %rax
    iretq

# CPU の例外（0〜31 番）: 何番かを画面に出して止まる。入り口は番号ごとに 1 つずつ（番号を積んでから共通の処理へ）
    .altmacro
    .macro exception_stub n
exception_\n:
    push $\n
    jmp exception
    .endm
    .set n, 0
    .rept 32
    exception_stub %n
    .set n, n + 1
    .endr

exception:
    cli
    mov $0x040f, %ax                    # 赤地に白
    call con_color
    mov $str_exception, %rsi
    call con_puts
    pop %rax                            # 例外の番号（6 = 存在しない命令、13 = 一般保護、14 = ページフォールト など）
    call con_print_u
    mov $str_stopped, %rsi
    call con_puts
1:  hlt
    jmp 1b

    .section .rodata
str_exception: .asciz "\n*** CPU exception "
str_stopped:   .asciz ". stopped ***\n"
    .align 8
    .macro stub_address n
    .quad exception_\n
    .endm
exception_stubs:
    .set n, 0
    .rept 32
    stub_address %n
    .set n, n + 1
    .endr
    .align 8
idt_ptr:
    .word 48 * 16 - 1
    .quad idt

    .section .bss
    .align 16
idt:   .skip 48 * 16
    .align 8
ticks: .skip 8                          # 起動してからのタイマーの回数（1 回 = 10ms）

    .section .note.GNU-stack, "", @progbits
