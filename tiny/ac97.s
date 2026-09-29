# 音: AC97（Intel ICH の AC97。PCI のサウンドカード）から PCM（48,000 Hz・16 ビット・ステレオ）を流し続ける。
#
# 音の中身は C の sound_fill(バッファ, サンプル数) がモノラルで作る（ソフト音源、sound.c）。ここで左右に写す。
# カードは「バッファの場所の一覧」（32 個）を順に読んで鳴らす。一覧は 2 つのバッファを交互に指し、
# 1 つ鳴らし終えるたびに割り込みが来るので、鳴らし終えた方を次の音で埋め、一覧の終わりを 1 つ先へ送る（止まらないように）。
# カードは自分でメモリから読む（バスマスタ）ので、ISA の DMA と違い、QEMU の処理が詰まらない
#
# カードは PCI の中から探す（種類 04 = マルチメディア、01 = 音）。ポートの場所と IRQ は BIOS が割り当てたものを読む
    .code64
    .set FRAMES, 1024                   # 1 つのバッファの長さ（左右で 1 組。21ms）
    # バスマスタのレジスタ（PCM 出力の箱）
    .set PO_BDBAR, 0x10                 # 一覧の番地
    .set PO_CIV, 0x14                   # 今鳴らしている番号
    .set PO_LVI, 0x15                   # 一覧の最後の番号
    .set PO_SR, 0x16                    # 状態（1 を書くと消える）
    .set PO_CR, 0x1b                    # 制御
    .set GLOB_CNT, 0x2c

    .text
    .global sound_init, sound_ok

# pci_read: PCI の設定レジスタ（eax = 番地）を読んで eax に
pci_read:
    push %rdx
    mov $0xcf8, %dx
    out %eax, %dx
    mov $0xcfc, %dx
    in %dx, %eax
    pop %rdx
    ret

# pci_write: PCI の設定レジスタ（eax = 番地）に ecx を書く
pci_write:
    push %rax
    push %rdx
    mov $0xcf8, %dx
    out %eax, %dx
    mov $0xcfc, %dx
    mov %ecx, %eax
    out %eax, %dx
    pop %rdx
    pop %rax
    ret

# sound_init: カードを見つけて鳴らし始める。無ければ何もしない（sound_ok = 0）
sound_init:
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rdi
    push %rsi
    push %r8
    push %r9
    push %r10
    push %r11
    mov $0x80000000, %ebx               # バス 0 の 32 台を順に見る（番地 = 0x80000000 | 台 << 11 | レジスタ）
1:  mov %ebx, %eax
    call pci_read
    cmp $0xffff, %ax                    # 誰もいない
    je 2f
    lea 8(%rbx), %eax                   # 種類（上位 16 ビット）
    call pci_read
    shr $16, %eax
    cmp $0x0401, %ax
    je 3f
2:  add $1 << 11, %ebx
    cmp $0x80000000 + (32 << 11), %ebx
    jne 1b
    jmp 9f                              # 見つからない
3:  lea 0x10(%rbx), %eax                # BAR0: ミキサー（コーデック）のポート
    call pci_read
    and $~3, %eax
    mov %ax, nambar
    lea 0x14(%rbx), %eax                # BAR1: バスマスタのポート
    call pci_read
    and $~3, %eax
    mov %ax, nabmbar
    lea 0x3c(%rbx), %eax                # IRQ の番号
    call pci_read
    mov %al, irq
    lea 4(%rbx), %eax                   # I/O とバスマスタを使えるように
    call pci_read
    or $5, %eax
    mov %eax, %ecx
    lea 4(%rbx), %eax
    call pci_write
    # コーデック: リセットを外し、音量を上げる
    movzwl nabmbar, %edx
    add $GLOB_CNT, %edx
    mov $2, %eax                        # コールドリセットを外す
    out %eax, %dx
    mov $100, %ecx
4:  in $0x80, %al
    loop 4b
    movzwl nambar, %edx                 # コーデックのリセット（何を書いてもよい）
    xor %eax, %eax
    out %ax, %dx
    add $2, %edx                        # 全体の音量: 0 dB、消音しない
    out %ax, %dx
    movzwl nambar, %edx                 # PCM の音量: 0 dB（0x0808）
    add $0x18, %edx
    mov $0x0808, %ax
    out %ax, %dx
    # バスマスタ: 止めてリセットし、一覧を作る
    movzwl nabmbar, %edx
    add $PO_CR, %edx
    mov $2, %al                         # レジスタのリセット
    out %al, %dx
5:  in %dx, %al
    test $2, %al
    jnz 5b
    xor %ecx, %ecx                      # 一覧: i 番は buffer[i % 2]、長さ（16 ビットの数）、終わったら割り込み
6:  mov %ecx, %eax
    and $1, %eax
    imul $FRAMES * 4, %eax
    add $buffer, %eax
    mov %eax, bdl(,%rcx,8)
    movw $FRAMES * 2, bdl + 4(,%rcx,8)
    movw $0x8000, bdl + 6(,%rcx,8)
    inc %ecx
    cmp $32, %ecx
    jne 6b
    xor %edi, %edi                      # 初めの音で 2 つとも埋める
    call fill
    mov $1, %edi
    call fill
    movzwl nabmbar, %edx
    add $PO_BDBAR, %edx
    mov $bdl, %eax
    out %eax, %dx
    movzwl nabmbar, %edx
    add $PO_LVI, %edx
    mov $31, %al
    out %al, %dx
    movzbl irq, %edi                    # 割り込みの入り口を作って通す
    add $32, %edi
    mov $ac97_isr, %rsi
    call idt_set
    movzbl irq, %edi
    call irq_unmask
    movzwl nabmbar, %edx                # 鳴らし始める（動かす・終わったら割り込み）
    add $PO_CR, %edx
    mov $0x11, %al
    out %al, %dx
    movb $1, sound_ok
9:  pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rsi
    pop %rdi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    ret

# fill: バッファ edi（0 か 1）を次の音で埋める。C がモノラルで作ったものを、左右に写す
fill:
    push %rdi
    push %rsi
    push %rcx
    push %rax
    push %rdi
    mov $mono, %rdi
    mov $FRAMES, %esi
    call sound_fill
    pop %rax
    imul $FRAMES * 4, %eax
    lea buffer(%rax), %rdi
    mov $mono, %rsi
    mov $FRAMES, %ecx
1:  lodsw
    stosw
    stosw
    loop 1b
    pop %rax
    pop %rcx
    pop %rsi
    pop %rdi
    ret

# 1 つ鳴らし終えた: 鳴らし終えた方を埋め、一覧の最後を今の 1 つ前にする（いつまでも最後に届かない）。
# C を呼ぶので、C が壊してよいレジスタを全部取っておく
ac97_isr:
    push %rax
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    movzwl nabmbar, %edx                # 状態を消す（1 を書く）
    add $PO_SR, %edx
    in %dx, %ax
    push %rax
    mov $0x1c, %ax
    out %ax, %dx
    pop %rax
    test $0x08, %al                     # バッファを鳴らし終えた
    jz 1f
    movzwl nabmbar, %edx
    add $PO_CIV, %edx
    in %dx, %al                         # 今鳴らしている番号。1 つ前が鳴らし終えた方
    movzbl %al, %esi
    lea 31(%rsi), %edi
    and $31, %edi
    push %rdi
    and $1, %edi
    call fill
    pop %rax
    movzwl nabmbar, %edx
    add $PO_LVI, %edx
    out %al, %dx
1:  mov $0x20, %al                      # 割り込みの終わりを PIC に（8 以上なら 2 つ目にも）
    cmpb $8, irq
    jb 2f
    out %al, $0xa0
2:  out %al, $0x20
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
irq:      .byte 0
nambar:   .word 0
nabmbar:  .word 0

    .section .bss
    .balign 8
bdl:      .skip 32 * 8                  # バッファの場所の一覧（番地 32 ビット、長さ 16 ビット、印 16 ビット）
mono:     .skip FRAMES * 2
    .balign 16
buffer:   .skip FRAMES * 4 * 2          # 16 ビット × 左右 × 2 つ

    .section .note.GNU-stack, "", @progbits
