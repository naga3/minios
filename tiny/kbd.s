# キーボード（PS/2）。キーボードの割り込みで番号（スキャンコード）を受け取って文字に変え、貯めておく。英語配列
#
# 貯めるのは文字と、特別なキーの番号: 矢印キーは KEY_UP・KEY_DOWN・KEY_LEFT・KEY_RIGHT（0x80〜0x83）、Esc は 27
# 関数の約束: 返す文字は al。使ったレジスタは戻す（rax を除く）
    .code64
    .set KEY_UP,    0x80
    .set KEY_DOWN,  0x81
    .set KEY_LEFT,  0x82
    .set KEY_RIGHT, 0x83
    .text
    .global getc, getc_nowait, kbd_scancode

# getc: キーが来るまで待って al に返す。待つ間は hlt で CPU を休める
getc:
1:  call getc_nowait
    test %al, %al
    jnz 2f
    hlt
    jmp 1b
2:  ret

# getc_nowait: 貯まっていれば 1 つ取り出して al に返す。無ければ 0（ゲームで使う）
getc_nowait:
    push %rbx
    cli                                 # 割り込み（貯める側）と取り合わないように
    movzbl tail, %ebx
    xor %eax, %eax
    cmp head, %bl
    je 1f
    mov buffer(%rbx), %al
    incb tail
1:  sti
    pop %rbx
    ret

# kbd_scancode: キーボードの割り込みから呼ばれる。al = スキャンコード（セット 1）
kbd_scancode:
    push %rbx
    cmpb $0, extended
    jne 5f
    cmp $0xe0, %al                      # 次の番号は矢印キーなど（前置き）
    je 6f
    cmp $0x2a, %al                      # 左 Shift を押した
    je 2f
    cmp $0x36, %al                      # 右 Shift を押した
    je 2f
    cmp $0xaa, %al                      # 左 Shift を離した
    je 3f
    cmp $0xb6, %al                      # 右 Shift を離した
    je 3f
    test $0x80, %al                     # 離したとき（上位ビットが立つ）は無視
    jnz 9f
    movzbl %al, %ebx
    cmp $0x3a, %ebx                     # 表にない番号
    jae 9f
    cmpb $0, shift
    jne 1f
    mov keymap(%rbx), %al
    jmp 8f
1:  mov keymap_shift(%rbx), %al
    jmp 8f
2:  movb $1, shift
    jmp 9f
3:  movb $0, shift
    jmp 9f
5:  movb $0, extended                   # 前置きの次: 矢印キー
    cmp $0x48, %al
    mov $KEY_UP, %bl
    je 7f
    cmp $0x50, %al
    mov $KEY_DOWN, %bl
    je 7f
    cmp $0x4b, %al
    mov $KEY_LEFT, %bl
    je 7f
    cmp $0x4d, %al
    mov $KEY_RIGHT, %bl
    je 7f
    jmp 9f
6:  movb $1, extended
    jmp 9f
7:  mov %bl, %al
8:  test %al, %al                       # 文字にならないキー（Ctrl、Alt など）
    jz 9f
    movzbl head, %ebx                   # 貯める（いっぱいなら捨てる）
    inc %bl
    cmp tail, %bl
    je 9f
    dec %bl
    mov %al, buffer(%rbx)
    incb head
9:  pop %rbx
    ret

    .section .rodata
# スキャンコード（セット 1）から文字へ。0x00〜0x39
keymap:
    .byte 0, 27                                                       # 0x00 なし、0x01 Esc
    .ascii "1234567890-="                                             # 0x02〜0x0d
    .byte '\b', '\t'                                                  # 0x0e バックスペース、0x0f Tab
    .ascii "qwertyuiop[]"                                             # 0x10〜0x1b
    .byte '\n', 0                                                     # 0x1c Enter、0x1d Ctrl
    .ascii "asdfghjkl;'`"                                             # 0x1e〜0x29
    .byte 0                                                           # 0x2a 左 Shift
    .ascii "\\zxcvbnm,./"                                             # 0x2b〜0x35
    .byte 0, '*', 0, ' '                                              # 0x36 右 Shift、0x37 *、0x38 Alt、0x39 空白
keymap_shift:
    .byte 0, 27
    .ascii "!@#$%^&*()_+"
    .byte '\b', '\t'
    .ascii "QWERTYUIOP{}"
    .byte '\n', 0
    .ascii "ASDFGHJKL:\"~"
    .byte 0
    .ascii "|ZXCVBNM<>?"
    .byte 0, '*', 0, ' '

    .section .data
shift:    .byte 0
extended: .byte 0                       # 0xE0 の前置きを受け取った
head:     .byte 0                       # 貯める場所（割り込みが書く）
tail:     .byte 0                       # 取り出す場所
    .section .bss
buffer:   .skip 256                     # 貯めておく輪（head と tail は 1 バイトなので、256 で自然に一周する）

    .section .note.GNU-stack, "", @progbits
