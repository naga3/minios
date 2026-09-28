# minios tiny: グラフィックの画面（320 × 200・256 色）に、自作のフォントで文字を描く。
# キーボードから 1 行読んで、そのまま返す（アセンブリだけ）
#
# 関数の約束（このカーネルの中の決まり）: 1 文字は al、文字列は rsi、書き込み先は rdi。返り値は rax。
# グラフィックの関数は x = rdi、y = rsi、幅 = rdx、高さ = rcx、色 = r8b
    .code64
    .text
    .global main
main:
    cld                                 # 文字列の命令（lodsb など）が前へ進むように
    call screen_init
    mov $0x0107, %ax                    # 背景は青（1）、文字は明るい灰色（7）
    call con_color
    call con_clear
    mov $0x010e, %ax                    # タイトルは黄色（14）
    call con_color
    mov $title, %rsi
    call con_puts
    mov $0x0107, %ax
    call con_color
    call palette_bar
    mov $subtitle, %rsi
    call con_puts
1:  mov $prompt, %rsi
    call con_puts
    mov $line, %rdi
    call read_line
    mov $line, %rsi
    call con_puts
    mov $newline, %rsi
    call con_puts
    jmp 1b

# palette_bar: 2 行目に 16 色の四角を並べる（グラフィックの画面で描けることの見本）
palette_bar:
    push %rdi
    push %rsi
    push %rdx
    push %rcx
    push %r8
    xor %r8d, %r8d
    mov $8, %esi                        # 2 行目（y = 8〜15）
1:  mov %r8d, %edi
    imul $20, %edi
    mov $20, %edx
    mov $6, %ecx
    call fill_rect
    inc %r8d
    cmp $16, %r8d
    jne 1b
    call present
    pop %r8
    pop %rcx
    pop %rdx
    pop %rsi
    pop %rdi
    ret

# read_line: rdi に 1 行読む（最大 37 文字、0 で終える）。打った文字はその場で表示し、バックスペースで消す。長さを rax に返す
read_line:
    push %rbx
    push %rsi
    xor %ebx, %ebx                      # rbx = 今の長さ
2:  call getc
    cmp $'\n', %al
    je 4f
    cmp $'\b', %al
    je 3f
    cmp $37, %rbx                       # 「> 」と合わせて 1 行（40 文字）に収める
    jae 2b
    mov %al, (%rdi,%rbx)
    inc %rbx
    call con_putc
    jmp 2b
3:  test %rbx, %rbx
    jz 2b
    dec %rbx
    mov $erase, %rsi                    # 1 つ戻って空白で消し、また戻る
    call con_puts
    jmp 2b
4:  movb $0, (%rdi,%rbx)
    mov $newline, %rsi
    call con_puts
    mov %rbx, %rax
    pop %rsi
    pop %rbx
    ret

    .section .rodata
title:    .asciz "minios tiny  320x200 256 colors\n"
subtitle: .asciz "\nThe quick brown fox jumps over\nthe lazy dog. 0123456789 !?@#$%&*\n\n"
prompt:   .asciz "> "
newline:  .asciz "\n"
erase:    .asciz "\b \b"

    .section .bss
line:     .skip 40

    .section .note.GNU-stack, "", @progbits
