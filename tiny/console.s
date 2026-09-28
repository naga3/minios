# 文字: 裏画面に 8 × 8 のフォントで文字を描く。40 文字 × 25 行。改行、バックスペース、スクロール、下線のカーソル
#
# 関数の約束: 1 文字は al、文字列は rsi。使ったレジスタは戻す（rax を除く）
    .code64
    .set COLS, 40
    .set ROWS, 25
    .set W, 320

    .text
    .global con_putc, con_puts, con_clear, con_color, con_goto, con_cursor, con_print_u

# con_color: 文字の色（al）と背景の色（ah）を決める
con_color:
    mov %al, fg
    mov %ah, bg
    ret

# con_goto: 文字を書く位置を al 列・ah 行に動かす
con_goto:
    push %rax
    call cursor_off
    pop %rax
    mov %al, col
    mov %ah, row
    jmp cursor_on

# con_cursor: カーソルを出す（al = 1）・隠す（al = 0）。ゲームの間は隠す
con_cursor:
    push %rax
    call cursor_off
    pop %rax
    mov %al, cursor_visible
    jmp cursor_on

# con_print_u: rax を 10 進で書く（画面へ写す）
con_print_u:
    push %rdx
    push %rsi
    push %rcx
    mov $num_buffer + 20, %rsi
    movb $0, (%rsi)
    mov $10, %ecx
1:  xor %edx, %edx
    div %rcx
    add $'0', %dl
    dec %rsi
    mov %dl, (%rsi)
    test %rax, %rax
    jnz 1b
    call con_puts
    pop %rcx
    pop %rsi
    pop %rdx
    ret

# con_clear: 背景の色で消して、左上へ
con_clear:
    push %r8
    movzbl bg, %r8d
    call clear
    movb $0, row
    movb $0, col
    call cursor_on
    call present
    pop %r8
    ret

# con_puts: rsi の文字列（0 で終わる）を書き、最後に画面へ写す
con_puts:
    push %rax
    push %rsi
1:  lodsb
    test %al, %al
    jz 2f
    call putc_nopresent
    jmp 1b
2:  call present
    pop %rsi
    pop %rax
    ret

# con_putc: al の 1 文字を書き、画面へ写す
con_putc:
    call putc_nopresent
    jmp present

putc_nopresent:
    push %rdi
    push %rsi
    push %rax                           # cursor_off（fill_rect）は rax を壊すので、文字を取っておく
    call cursor_off
    pop %rax
    cmp $'\n', %al
    je 2f
    cmp $'\r', %al
    je 1f
    cmp $'\b', %al
    je 3f
    movzbl col, %edi                    # 文字を描く
    shl $3, %edi
    movzbl row, %esi
    shl $3, %esi
    call draw_char
    incb col
    cmpb $COLS, col
    jb 4f
2:  incb row                            # 改行（右端まで来たときも）
1:  movb $0, col
    cmpb $ROWS, row
    jb 4f
    call scroll
    movb $ROWS - 1, row
    jmp 4f
3:  cmpb $0, col                        # バックスペース: 1 つ左へ（消すのは呼び出し側。"\b \b"）
    je 4f
    decb col
4:  call cursor_on
    pop %rsi
    pop %rdi
    ret

# draw_char: al の文字を (rdi, rsi) に描く（点は fg、それ以外は bg）
draw_char:
    push %rbx
    push %rcx
    push %rdx
    push %r8
    push %r9
    push %r10
    movzbl %al, %ebx
    sub $0x20, %ebx                     # フォントは 0x20 からの 95 文字。それ以外は ? にする
    cmp $95, %ebx
    jb 1f
    mov $'?' - 0x20, %ebx
1:  lea font(,%rbx,8), %r9              # r9 = この文字の 8 バイト
    xor %r10d, %r10d                    # r10 = 行
2:  movzbl (%r9,%r10), %edx             # この行の点（上位のビットが左）
    xor %ecx, %ecx                      # ecx = 列
3:  movzbl bg, %r8d
    test $0x80, %dl
    jz 4f
    movzbl fg, %r8d
4:  push %rdi
    push %rsi
    add %rcx, %rdi
    add %r10, %rsi
    call put_pixel
    pop %rsi
    pop %rdi
    shl $1, %dl
    inc %ecx
    cmp $8, %ecx
    jne 3b
    inc %r10
    cmp $8, %r10
    jne 2b
    pop %r10
    pop %r9
    pop %r8
    pop %rdx
    pop %rcx
    pop %rbx
    ret

# scroll: 1 行（8 点）ぶん上にずらし、一番下の行を背景の色で空ける
scroll:
    push %rcx
    push %rsi
    push %rdi
    push %rdx
    push %r8
    mov $backbuf + W * 8, %rsi
    mov $backbuf, %rdi
    mov $W * 8 * (ROWS - 1), %ecx
    rep movsb
    xor %edi, %edi
    mov $8 * (ROWS - 1), %esi
    mov $W, %edx
    mov $8, %ecx
    movzbl bg, %r8d
    call fill_rect
    pop %r8
    pop %rdx
    pop %rdi
    pop %rsi
    pop %rcx
    ret

# カーソル: 今の位置の一番下の行に線を引く・消す（隠しているときは引かない）
cursor_on:
    cmpb $0, cursor_visible
    je 2f
    push %r8
    movzbl fg, %r8d
    jmp 1f
2:  ret
cursor_off:
    push %r8
    movzbl bg, %r8d
1:  push %rdi
    push %rsi
    push %rdx
    push %rcx
    movzbl col, %edi
    shl $3, %edi
    movzbl row, %esi
    shl $3, %esi
    add $7, %esi
    mov $8, %edx
    mov $1, %ecx
    call fill_rect
    pop %rcx
    pop %rdx
    pop %rsi
    pop %rdi
    pop %r8
    ret

    .section .data
row: .byte 0
col: .byte 0
fg:  .byte 7                            # 明るい灰色
cursor_visible: .byte 1
bg:  .byte 0                            # 黒

    .section .bss
num_buffer: .skip 21

    .section .note.GNU-stack, "", @progbits
