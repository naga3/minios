# 音: Sound Blaster 16 から PCM（22,050 Hz・16 ビット・モノラル）を流し続ける。
#
# 音の中身は C の sound_fill(バッファ, サンプル数) が作る（ソフト音源、sound.c）。
# バッファを半分ずつに分けて、DMA（16 ビットのチャンネル 5）で繰り返し送る（auto-init）。
# 半分を鳴らし終えるたびに割り込み（IRQ5）が来るので、鳴らし終えた方の半分を次の音で埋める。
# QEMU の Sound Blaster 16 は、ポート 0x220、IRQ5、16 ビットの DMA はチャンネル 5
    .code64
    .set SB, 0x220
    .set RATE, 22050
    .set HALF, 512                      # 半分のサンプル数（23ms）

    .text
    .global sound_init, sb16_isr, sound_ok

# sound_init: カードを見つけて鳴らし始める。無ければ何もしない（sound_ok = 0）
sound_init:
    push %rax
    push %rcx
    push %rdx
    push %rdi
    push %rsi
    push %r8
    push %r9
    push %r10
    push %r11
    mov $SB + 6, %dx                    # DSP のリセット: 1 を書いて少し待ち、0 を書く
    mov $1, %al
    out %al, %dx
    mov $100, %ecx
1:  in $0x80, %al                       # 待つ（1 回 1µs ほど）
    loop 1b
    xor %al, %al
    out %al, %dx
    mov $100000, %ecx                   # 読めるようになったら 0xAA が返る
2:  mov $SB + 0xe, %dx
    in %dx, %al
    test $0x80, %al
    jnz 3f
    loop 2b
    jmp 9f                              # カードが無い
3:  mov $SB + 0xa, %dx
    in %dx, %al
    cmp $0xaa, %al
    jne 9f
    lea buffer, %rdi                    # 初めの音でバッファ全体を埋める
    mov $HALF * 2, %esi
    call sound_fill
    # DMA コントローラ（16 ビット側）: チャンネル 5 を、バッファから読んで繰り返し送るように
    mov $0x05, %al                      # チャンネル 5 を止める
    out %al, $0xd4
    xor %al, %al                        # 上下のバイトの順番を初めに戻す
    out %al, $0xd8
    mov $0x59, %al                      # 1 回ずつ・auto-init・メモリから読む・チャンネル 5
    out %al, $0xd6
    lea buffer, %rax                    # 16 ビットの DMA は、番地を 2 で割った値（語の番地）で書く
    shr $1, %rax
    out %al, $0xc4
    shr $8, %rax
    out %al, $0xc4
    mov $HALF * 2 - 1, %ax              # 語の数 - 1
    out %al, $0xc6
    mov %ah, %al
    out %al, $0xc6
    lea buffer, %rax                    # ページ: 番地のビット 17〜23
    shr $16, %rax
    and $0xfe, %al
    out %al, $0x8b
    mov $0x01, %al                      # チャンネル 5 を動かす
    out %al, $0xd4
    # DSP: 出す速さ、16 ビット・auto-init で出し始める
    mov $0x41, %al
    call dsp_write
    mov $RATE >> 8, %al
    call dsp_write
    mov $RATE & 0xff, %al
    call dsp_write
    mov $0xb6, %al                      # 16 ビット・出力・auto-init・FIFO
    call dsp_write
    mov $0x10, %al                      # モノラル・符号付き
    call dsp_write
    mov $(HALF - 1) & 0xff, %al         # 1 回の割り込みまでのサンプル数 - 1
    call dsp_write
    mov $(HALF - 1) >> 8, %al
    call dsp_write
    in $0x21, %al                       # PIC で IRQ5 を通す
    and $~0x20, %al
    out %al, $0x21
    movb $1, sound_ok
9:  pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rsi
    pop %rdi
    pop %rdx
    pop %rcx
    pop %rax
    ret

# dsp_write: al を DSP に書く（書けるようになるまで待つ）
dsp_write:
    push %rax
    push %rdx
    mov $SB + 0xc, %dx
1:  in %dx, %al
    test $0x80, %al
    jnz 1b
    mov 8(%rsp), %rax
    out %al, %dx
    pop %rdx
    pop %rax
    ret

# 半分を鳴らし終えた: その半分を次の音で埋める。C を呼ぶので、C が壊してよいレジスタを全部取っておく
sb16_isr:
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    mov $SB + 0xf, %dx                  # 16 ビットの割り込みを受け取ったと知らせる
    in %dx, %al
    movzbl half, %eax                   # 鳴らし終えた方（0 か 1）
    imul $HALF * 2, %eax
    lea buffer(%rax), %rdi
    mov $HALF, %esi
    call sound_fill
    xorb $1, half
    mov $0x20, %al                      # 割り込みの終わりを PIC に知らせる
    out %al, $0x20
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rax
    iretq

    .section .data
sound_ok: .byte 0                       # カードがあって鳴らしている
half:     .byte 0                       # 次に鳴らし終える半分

    .section .bss
    .balign 4096                        # DMA は 128KB の境目をまたげない。4KB に揃えれば、2KB のバッファはまたがない
buffer:   .skip HALF * 2 * 2            # 16 ビット × 2 つの半分

    .section .note.GNU-stack, "", @progbits
