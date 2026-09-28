// 起動の最初。QEMU（Multiboot）は 32 ビットのプロテクトモードでここ（_start）を呼ぶ。
// ページテーブルを作って 64 ビット（ロングモード）に切り替え、kmain（C）を呼ぶ
    .set MB_MAGIC, 0x1BADB002
    .set MB_FLAGS, 0x3               // モジュールをページ境界に置く + メモリの情報をもらう
    .section .multiboot, "a"
    .align 4
    .long MB_MAGIC, MB_FLAGS, -(MB_MAGIC + MB_FLAGS)

    .section .bss
    .align 4096
    .global pml4
pml4:   .skip 4096                   // ページテーブル（4 段）。最初の 4GB をそのままの番地で使えるようにする（2MB のページ）
pdpt:   .skip 4096
pd:     .skip 4096 * 4
stack:  .skip 16384
stack_top:

    .section .data
    .align 16
gdt:                                 // 64 ビットのコードとデータのセグメント
    .quad 0
    .quad 0x00af9a000000ffff         // 0x08: 64 ビットのコード（カーネル）
    .quad 0x00cf92000000ffff         // 0x10: データ（カーネル）
gdt_ptr:
    .word gdt_ptr - gdt - 1
    .quad gdt

    .section .text
    .code32
    .global _start
_start:
    mov $stack_top, %esp
    mov %eax, %edi                   // Multiboot の magic と情報の番地を kmain の引数として取っておく
    mov %ebx, %esi

    // PML4[0] → PDPT、PDPT[0..3] → PD 4 枚、PD の各項目が 2MB ずつ（計 4GB）
    mov $pdpt, %eax
    or $0x3, %eax                    // 存在する + 書き込める
    mov %eax, pml4
    mov %eax, pml4 + 256 * 8         // 同じ 4GB を高い番地（0xFFFF800000000000〜）にも写す。カーネルは物理メモリをここから触る
    mov $0, %ecx
1:  mov %ecx, %eax
    shl $12, %eax
    add $pd, %eax
    or $0x3, %eax
    mov %eax, pdpt(,%ecx,8)
    inc %ecx
    cmp $4, %ecx
    jne 1b
    mov $0, %ecx
2:  mov %ecx, %eax
    shl $21, %eax                    // 2MB ずつ
    or $0x83, %eax                   // 存在する + 書き込める + 大きいページ
    mov %eax, pd(,%ecx,8)
    mov %ecx, %eax
    shr $11, %eax                    // 4GB を超える分の上位 32 ビット（i * 2MB >> 32）
    mov %eax, pd+4(,%ecx,8)
    inc %ecx
    cmp $2048, %ecx
    jne 2b

    mov $pml4, %eax                  // ページテーブルの場所
    mov %eax, %cr3
    mov %cr4, %eax                   // PAE（ロングモードの前提）
    or $0x20, %eax
    mov %eax, %cr4
    mov $0xC0000080, %ecx            // EFER.LME（ロングモードを有効に）
    rdmsr
    or $0x100, %eax
    wrmsr
    mov %cr0, %eax                   // ページングを有効に → ロングモードに入る
    or $0x80000000, %eax
    mov %eax, %cr0

    lgdt gdt_ptr
    ljmp $0x08, $long_mode

    .code64
long_mode:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    mov $stack_top, %rsp
    call kmain
3:  hlt
    jmp 3b
    .section .note.GNU-stack, "", @progbits  // スタックを実行可能にしない（リンカの警告を消す）
