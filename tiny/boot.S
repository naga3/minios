// QEMU（Multiboot）は 32 ビットでここを呼ぶ。最初の 1GB をそのままの番地で写すページテーブルを作り、64 ビットに切り替えて main を呼ぶ
    .section .multiboot, "a"
    .align 4
    .long 0x1BADB002, 0, -0x1BADB002    // Multiboot の印（magic, flags, checksum）

    .section .bss
    .align 4096
pml4:   .skip 4096
pdpt:   .skip 4096
pd:     .skip 4096                       // 2MB のページ × 512 = 1GB
stack:  .skip 16384
stack_top:

    .section .data
gdt:    .quad 0, 0x00af9a000000ffff      // 空、64 ビットのコード
gdt_ptr: .word 15
        .quad gdt

    .section .text
    .code32
    .global _start
_start:
    mov $stack_top, %esp
    mov $pdpt + 3, %eax                  // 表をつなぐ（+3 = 存在する・書き込める）
    mov %eax, pml4
    mov $pd + 3, %eax
    mov %eax, pdpt
    mov $0, %ecx
1:  mov %ecx, %eax                       // pd[i] = i × 2MB（+0x83 = 存在する・書き込める・大きいページ）
    shl $21, %eax
    or $0x83, %eax
    mov %eax, pd(,%ecx,8)
    inc %ecx
    cmp $512, %ecx
    jne 1b
    mov $pml4, %eax                      // ページテーブルの場所
    mov %eax, %cr3
    mov %cr4, %eax                       // PAE を有効に
    or $0x20, %eax
    mov %eax, %cr4
    mov $0xC0000080, %ecx                // ロングモードを有効に（EFER.LME）
    rdmsr
    or $0x100, %eax
    wrmsr
    mov %cr0, %eax                       // ページングを有効に → 64 ビットに入る
    or $0x80000000, %eax
    mov %eax, %cr0
    lgdt gdt_ptr
    ljmp $0x08, $long_mode

    .code64
long_mode:
    call main
2:  hlt
    jmp 2b

    .section .note.GNU-stack, "", @progbits
