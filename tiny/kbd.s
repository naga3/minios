# キーボード（PS/2）。I/O ポート 0x64 の状態を見張り、0x60 からスキャンコード（キーの番号）を読んで文字にする。英語配列
#
# 関数の約束: 返す文字は al。使ったレジスタは戻す（rax を除く）
    .code64
    .text
    .global getc

# getc: 文字になるキーが押されるまで待って、al に返す。Enter は '\n'、バックスペースは '\b'
getc:
    push %rbx
1:  in $0x64, %al                       # ビット 0 = 読めるものがある
    test $1, %al
    jz 1b
    in $0x60, %al
    cmp $0xe0, %al                      # 矢印キーなどの前置き。今は使わない
    je 1b
    cmp $0x2a, %al                      # 左 Shift を押した
    je 2f
    cmp $0x36, %al                      # 右 Shift を押した
    je 2f
    cmp $0xaa, %al                      # 左 Shift を離した
    je 3f
    cmp $0xb6, %al                      # 右 Shift を離した
    je 3f
    test $0x80, %al                     # 離したとき（上位ビットが立つ）は無視
    jnz 1b
    movzbl %al, %ebx
    cmp $0x3a, %ebx                     # 表にない番号
    jae 1b
    cmpb $0, shift
    jne 4f
    mov keymap(%rbx), %al
    jmp 5f
4:  mov keymap_shift(%rbx), %al
5:  test %al, %al                       # 文字にならないキー（Ctrl、Alt など）
    jz 1b
    pop %rbx
    ret
2:  movb $1, shift
    jmp 1b
3:  movb $0, shift
    jmp 1b

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
shift: .byte 0

    .section .note.GNU-stack, "", @progbits
