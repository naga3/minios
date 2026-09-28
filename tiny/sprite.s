# スプライト: 16 × 16 点の絵を、裏画面の好きな位置に描く（画面からはみ出す分は切る。左右の反転もできる）。
#
# 絵は 1 点を 1 文字で、16 文字 × 16 行の文字列として書く（フォントと同じく、ソースを見れば形が分かる）:
#   '.' は透明（後ろが見える）
#   0〜9・a〜f は DOS の 16 色（0 黒、1 青、2 緑、3 水色、4 赤、5 紫、6 茶、7 明るい灰色、
#                               8 暗い灰色、9 明るい青、a 明るい緑、b 明るい水色、c 明るい赤、d 明るい紫、e 黄、f 白）
#   そのほかの文字は extra_colors の表の色（256 色のパレットの番号）
#
# 関数の約束: x は rdi、y は rsi（どちらも負でもよい）、絵は rdx、rcx のビット 0 が 1 なら左右反転・ビット 1 が 1 なら上下反転（C の呼び方と同じ並び）。使ったレジスタは戻す（rax を除く）
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
    push %r8
    push %r9
    push %r10
    push %r11
    mov %ecx, %r8d                      # r8 = 反転するか（rcx は x に使う）
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
3:  mov %r10, %rax                      # 上下反転なら下の行から読む
    test $2, %r8b
    jz 6f
    mov $15, %eax
    sub %r10, %rax
6:  shl $4, %rax
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
    pop %r8
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
    # 可愛いキャラクター用: 輪郭は真っ黒でなく濃い同系色、塗りは明るい・中・影の 3 段、頬のピンク
    .byte 'h', 16 + 1 * 36 + 1 * 6 + 2  # 輪郭（紺）
    .byte 'z', 16 + 1 * 36 + 0 * 6 + 0  # 輪郭（こげ茶）
    .byte 'j', 16 + 1 * 36 + 0 * 6 + 2  # 輪郭（濃い紫）
    .byte 'v', 16 + 0 * 36 + 2 * 6 + 1  # 輪郭（濃い緑）
    .byte 'q', 16 + 3 * 36 + 0 * 6 + 1  # 濃い赤
    .byte 'p', 16 + 5 * 36 + 3 * 6 + 4  # 頬のピンク
    .byte 'l', 16 + 3 * 36 + 4 * 6 + 5  # 明るい青
    .byte 'm', 16 + 1 * 36 + 2 * 6 + 5  # 青
    .byte 'u', 16 + 0 * 36 + 1 * 6 + 3  # 濃い青
    .byte 'x', 16 + 5 * 36 + 4 * 6 + 3  # 肌色
    .byte 'y', 16 + 5 * 36 + 5 * 6 + 2  # 薄い黄
    .byte 'w', 16 + 5 * 36 + 5 * 6 + 4  # クリーム色
    .byte 'g', 16 + 4 * 36 + 4 * 6 + 4  # 薄い灰色
    .byte 't', 16 + 3 * 36 + 5 * 6 + 2  # 明るい緑
    .byte 'i', 16 + 1 * 36 + 4 * 6 + 1  # 緑
    .byte 0

    .section .bss
char_colors: .skip 128                  # 文字 → 色

    .section .note.GNU-stack, "", @progbits
