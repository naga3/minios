# 割り込み: IDT（割り込みの入り口の表）、割り込みコントローラ（PIC）、タイマー（PIT、1 秒に 100 回）
#
# 割り込みが来ると、CPU は今の仕事を止めて IDT の入り口に飛び、iretq で元の仕事に戻る。
# 使うのは タイマー（IRQ0 → 32 番）とキーボード（IRQ1 → 33 番）。ほかの装置（音など）は、ドライバが idt_set と irq_unmask で足す。
# CPU の例外（0〜31 番）が起きたら、知らせて止まる
    .code64
    .text
    .global irq_init, ticks, sleep_ticks, idt_set, irq_unmask

irq_init:
    push %rax
    push %rbx
    push %rcx
    push %rdi
    push %rsi
    xor %ecx, %ecx                      # IDT の 48 個の入り口を作る
1:  mov exception_stubs(,%rcx,8), %rsi
    cmp $32, %ecx
    jb 2f
    mov $spurious, %rsi
    cmp $32, %ecx
    jne 3f
    mov $timer_isr, %rsi
3:  cmp $33, %ecx
    jne 2f
    mov $kbd_isr, %rsi
2:  mov %ecx, %edi
    call idt_set
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
    pop %rsi
    pop %rdi
    pop %rcx
    pop %rbx
    pop %rax
    ret

# idt_set: 割り込みの番号 rdi の入り口を、rsi の番地にする（1 つ 16 バイト: 番地を 3 つに分けて書く）
idt_set:
    push %rax
    push %rdi
    shl $4, %rdi
    add $idt, %rdi
    mov %rsi, %rax
    mov %ax, (%rdi)                     # 番地の下位 16 ビット
    movw $0x08, 2(%rdi)                 # コードのセグメント（boot.s の GDT）
    movb $0, 4(%rdi)
    movb $0x8e, 5(%rdi)                 # 存在する、割り込みゲート
    shr $16, %rax
    mov %ax, 6(%rdi)                    # 番地の中ほど 16 ビット
    shr $16, %rax
    mov %eax, 8(%rdi)                   # 番地の上位 32 ビット
    movl $0, 12(%rdi)
    pop %rdi
    pop %rax
    ret

# irq_unmask: PIC で IRQ edi（0〜15）を通す。8 以上は 2 つ目の PIC で、1 つ目の IRQ2（つなぎ）も通す
irq_unmask:
    push %rax
    push %rcx
    push %rdx
    mov %edi, %ecx
    cmp $8, %ecx
    jb 1f
    sub $8, %ecx
    mov $0xa1, %dx
    in %dx, %al
    btr %ecx, %eax
    out %al, %dx
    mov $2, %ecx
1:  in $0x21, %al
    btr %ecx, %eax
    out %al, $0x21
    pop %rdx
    pop %rcx
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
