# 画面: 320 × 200・256 色。裏画面（メモリの中）に描いて、present で本物の画面に写す。
# 今は QEMU の VGA のモード 13h（0xA0000 から 1 点 1 バイト）にそのまま写す。
# 後で UEFI に移るときは、present を「GOP の画面に拡大して写す」に書き換えるだけで済むようにしてある
#
# 関数の約束: x は rdi、y は rsi、幅は rdx、高さは rcx、色は r8b。使ったレジスタは戻す（rax を除く）
    .code64
    .set W, 320
    .set H, 200
    .set VRAM, 0xa0000

    .text
    .global screen_init, present, put_pixel, fill_rect, clear, backbuf

# screen_init: VGA をモード 13h（320 × 200・256 色）に切り替え、パレットを用意する。
# 64 ビットでは BIOS を呼べないので、VGA のレジスタに直接書く
screen_init:
    push %rax
    push %rcx
    push %rdx
    push %rsi
    mov $0x3c2, %dx                     # その他の出力
    mov $0x63, %al
    out %al, %dx
    mov $0x3c4, %dx                     # シーケンサ
    mov $seq_regs, %rsi
    mov $5, %ecx
    call write_indexed
    mov $0x3d4, %dx                     # CRTC（まず書き込みの鍵を外す）
    mov $0x11, %al
    out %al, %dx
    inc %dx
    in %dx, %al
    and $0x7f, %al
    out %al, %dx
    dec %dx
    mov $crtc_regs, %rsi
    mov $25, %ecx
    call write_indexed
    mov $0x3ce, %dx                     # グラフィックコントローラ
    mov $gc_regs, %rsi
    mov $9, %ecx
    call write_indexed
    mov $0x3da, %dx                     # 属性コントローラ: 0x3DA を読むと「次は番号」の状態に戻る
    in %dx, %al
    mov $0x3c0, %dx
    mov $ac_regs, %rsi
    xor %ecx, %ecx
1:  mov %cl, %al
    out %al, %dx                        # 番号
    lodsb
    out %al, %dx                        # 値
    inc %ecx
    cmp $21, %ecx
    jne 1b
    mov $0x20, %al                      # 画面を表示する
    out %al, %dx
    call set_palette
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rax
    ret

# write_indexed: dx の番号のポートに 0 から順に番号を書き、dx+1 に rsi の値を ecx 個書く
write_indexed:
    xor %ah, %ah
1:  mov %ah, %al
    out %al, %dx
    inc %dx
    lodsb
    out %al, %dx
    dec %dx
    inc %ah
    dec %ecx
    jnz 1b
    ret

# set_palette: 256 色を決める。0〜15 は DOS の 16 色、16〜231 は 6 × 6 × 6 の色の立方体、232〜255 は灰色の濃淡。
# VGA の色は、赤・緑・青がそれぞれ 0〜63
set_palette:
    push %rbx
    mov $0x3c8, %dx
    xor %al, %al
    out %al, %dx                        # 0 番から順に
    mov $0x3c9, %dx
    mov $dos_colors, %rsi               # 0〜15
    mov $48, %ecx
1:  lodsb
    out %al, %dx
    loop 1b
    xor %ebx, %ebx                      # 16〜231: 番号 i から 赤 = i / 36、緑 = i / 6 % 6、青 = i % 6
2:  mov %ebx, %eax
    xor %edx, %edx
    mov $36, %ecx
    div %ecx                            # eax = 赤、edx = 残り
    mov %edx, %ecx
    movzbl cube(%eax), %eax
    mov $0x3c9, %dx
    out %al, %dx
    mov %ecx, %eax
    xor %edx, %edx
    push %rcx
    mov $6, %ecx
    div %ecx                            # eax = 緑、edx = 青
    pop %rcx
    push %rdx
    movzbl cube(%eax), %eax
    mov $0x3c9, %dx
    out %al, %dx
    pop %rax
    movzbl cube(%eax), %eax
    out %al, %dx
    inc %ebx
    cmp $216, %ebx
    jne 2b
    xor %ebx, %ebx                      # 232〜255: 灰色（0〜63 を 24 段に）
3:  mov %ebx, %eax
    imul $63, %eax
    xor %edx, %edx
    mov $23, %ecx
    div %ecx
    mov $0x3c9, %dx
    out %al, %dx
    out %al, %dx
    out %al, %dx
    inc %ebx
    cmp $24, %ebx
    jne 3b
    pop %rbx
    ret

# present: 裏画面を本物の画面に写す
present:
    push %rcx
    push %rsi
    push %rdi
    mov $backbuf, %rsi
    mov $VRAM, %rdi
    mov $W * H / 8, %ecx
    rep movsq
    pop %rdi
    pop %rsi
    pop %rcx
    ret

# put_pixel: (rdi, rsi) に r8b の色の点を打つ（画面の外なら何もしない）
put_pixel:
    cmp $W, %rdi
    jae 1f
    cmp $H, %rsi
    jae 1f
    imul $W, %rsi, %rax
    add %rdi, %rax
    mov %r8b, backbuf(%rax)
1:  ret

# fill_rect: (rdi, rsi) から幅 rdx・高さ rcx の四角を r8b の色で塗る（画面からはみ出す分は切る）
fill_rect:
    push %rbx
    push %rcx
    push %rdx
    push %rdi
    push %r9
    lea (%rdi,%rdx), %r9                # 右端を画面に収める
    cmp $W, %r9
    jbe 1f
    mov $W, %r9
1:  sub %rdi, %r9                       # r9 = 実際の幅
    jbe 4f
    lea (%rsi,%rcx), %rbx               # 下端を画面に収める
    cmp $H, %rbx
    jbe 2f
    mov $H, %rbx
2:  cmp %rsi, %rbx
    jbe 4f
    imul $W, %rsi, %rax                 # 左上の番地
    add %rdi, %rax
    lea backbuf(%rax), %rdi
    mov %rbx, %rdx
    sub %rsi, %rdx                      # rdx = 実際の高さ
    mov %r8b, %al
3:  mov %r9, %rcx
    push %rdi
    rep stosb
    pop %rdi
    add $W, %rdi
    dec %rdx
    jnz 3b
4:  pop %r9
    pop %rdi
    pop %rdx
    pop %rcx
    pop %rbx
    ret

# clear: 裏画面全体を r8b の色で塗る
clear:
    push %rcx
    push %rdi
    mov $backbuf, %rdi
    mov $W * H, %ecx
    mov %r8b, %al
    rep stosb
    pop %rdi
    pop %rcx
    ret

    .section .rodata
# モード 13h のレジスタの値（VGA の決まった並び）
seq_regs:  .byte 0x03, 0x01, 0x0f, 0x00, 0x0e
crtc_regs: .byte 0x5f, 0x4f, 0x50, 0x82, 0x54, 0x80, 0xbf, 0x1f, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
           .byte 0x9c, 0x0e, 0x8f, 0x28, 0x40, 0x96, 0xb9, 0xa3, 0xff
gc_regs:   .byte 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0f, 0xff
ac_regs:   .byte 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
           .byte 0x41, 0x00, 0x0f, 0x00, 0x00
# DOS の 16 色（赤、緑、青。それぞれ 0〜63）
dos_colors:
    .byte  0,  0,  0,   0,  0, 42,   0, 42,  0,   0, 42, 42     # 黒、青、緑、水色
    .byte 42,  0,  0,  42,  0, 42,  42, 21,  0,  42, 42, 42     # 赤、紫、茶、明るい灰色
    .byte 21, 21, 21,  21, 21, 63,  21, 63, 21,  21, 63, 63     # 暗い灰色、明るい青、明るい緑、明るい水色
    .byte 63, 21, 21,  63, 21, 63,  63, 63, 21,  63, 63, 63     # 明るい赤、明るい紫、黄、白
cube: .byte 0, 13, 25, 38, 50, 63       # 色の立方体の 6 段

    .section .bss
    .align 8
backbuf: .skip W * H                    # 裏画面

    .section .note.GNU-stack, "", @progbits
