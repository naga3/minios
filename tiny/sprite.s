# スプライト: 16 × 16 点の絵を、裏画面の好きな位置に描く（画面からはみ出す分は切る。左右の反転もできる）。
#
# 絵は 1 点を 1 文字で、16 文字 × 16 行の文字列として書く（フォントと同じく、ソースを見れば形が分かる）:
#   '.' は透明（後ろが見える）
#   0〜9・a〜f は DOS の 16 色（0 黒、1 青、2 緑、3 水色、4 赤、5 紫、6 茶、7 明るい灰色、
#                               8 暗い灰色、9 明るい青、a 明るい緑、b 明るい水色、c 明るい赤、d 明るい紫、e 黄、f 白）
#   そのほかの文字は extra_colors の表の色（256 色のパレットの番号）
#
# 関数の約束: x は rdi、y は rsi（どちらも負でもよい）、絵は rdx、r8b のビット 0 が 1 なら左右反転。使ったレジスタは戻す（rax を除く）
    .code64
    .set W, 320
    .set H, 200

    .text
    .global sprite_init, draw_sprite

# sprite_init: 文字 → 色 の表を作る
sprite_init:
    push %rcx
    push %rdi
    push %rsi
    mov $char_colors, %rdi
    mov $128, %ecx
    mov $0xff, %al                      # 0xff は透明
    rep stosb
    xor %ecx, %ecx                      # '0'〜'9' → 0〜9
1:  mov %cl, char_colors + '0'(%rcx)
    inc %ecx
    cmp $10, %ecx
    jne 1b
    xor %ecx, %ecx                      # 'a'〜'f' → 10〜15
2:  lea 10(%rcx), %eax
    mov %al, char_colors + 'a'(%rcx)
    inc %ecx
    cmp $6, %ecx
    jne 2b
    mov $extra_colors, %rsi             # そのほかの文字
3:  movzbl (%rsi), %ecx
    test %ecx, %ecx
    jz 4f
    mov 1(%rsi), %al
    mov %al, char_colors(%rcx)
    add $2, %rsi
    jmp 3b
4:  pop %rsi
    pop %rdi
    pop %rcx
    ret

# draw_sprite: (rdi, rsi) に rdx の絵を描く
draw_sprite:
    push %rbx
    push %rcx
    push %r9
    push %r10
    push %r11
    xor %r10d, %r10d                    # r10 = 絵の行
1:  lea (%rsi,%r10), %r11               # r11 = 画面の y
    cmp $H, %r11
    jae 5f                              # 画面の外（負の数は符号なしで比べると大きい）
    xor %r9d, %r9d                      # r9 = 絵の列
2:  lea (%rdi,%r9), %rcx                # rcx = 画面の x
    cmp $W, %rcx
    jae 4f
    mov %r9, %rbx                       # 左右反転なら右から読む
    test $1, %r8b
    jz 3f
    mov $15, %ebx
    sub %r9, %rbx
3:  mov %r10, %rax
    shl $4, %rax
    add %rax, %rbx
    movzbl (%rdx,%rbx), %ebx
    and $0x7f, %ebx
    mov char_colors(%rbx), %al
    cmp $0xff, %al                      # 透明
    je 4f
    imul $W, %r11, %rbx
    mov %al, backbuf(%rbx,%rcx)
4:  inc %r9
    cmp $16, %r9
    jne 2b
5:  inc %r10
    cmp $16, %r10
    jne 1b
    pop %r11
    pop %r10
    pop %r9
    pop %rcx
    pop %rbx
    ret

    .section .rodata
# 16 色のほかに使う色: 文字と、パレットの番号（16 + 赤 × 36 + 緑 × 6 + 青。それぞれ 0〜5）
extra_colors:
    .byte 'o', 16 + 5 * 36 + 3 * 6 + 0  # だいだい色
    .byte 'r', 16 + 4 * 36 + 1 * 6 + 0  # れんが色
    .byte 'n', 16 + 4 * 36 + 2 * 6 + 1  # 土の色
    .byte 'k', 16 + 3 * 36 + 1 * 6 + 0  # こげ茶
    .byte 's', 16 + 2 * 36 + 4 * 6 + 5  # 空の色
    .byte 0

    .section .bss
char_colors: .skip 128                  # 文字 → 色

    .section .note.GNU-stack, "", @progbits
