# minios tiny: シリアルから 1 行読んで、そのまま返す（アセンブリだけ）
#
# 関数の約束（このファイルの中だけの決まり）:
#   引数は rdi・rsi、1 文字は al。返り値は rax。rbx は呼ばれた側が守る。
    .code64
    .set COM1, 0x3f8
    .set LSR, COM1 + 5                  # 状態: ビット 0 = 文字が届いた、ビット 5 = 送れる

    .text
    .global main
main:
    cld                                 # 文字列の命令（lodsb）が前へ進むように
    mov $banner, %rsi
    call puts
1:  mov $prompt, %rsi
    call puts
    mov $line, %rdi
    call read_line
    mov $line, %rsi
    call puts
    mov $newline, %rsi
    call puts
    jmp 1b

# putc: al の 1 文字を送る（送れるようになるまで待つ）
putc:
    push %rdx
    push %rax
    mov $LSR, %dx
2:  in %dx, %al
    test $0x20, %al
    jz 2b
    pop %rax
    mov $COM1, %dx
    out %al, %dx
    pop %rdx
    ret

# getc: 1 文字届くまで待って、al に返す
getc:
    push %rdx
    mov $LSR, %dx
3:  in %dx, %al
    test $0x01, %al
    jz 3b
    mov $COM1, %dx
    in %dx, %al
    pop %rdx
    ret

# puts: rsi の文字列（0 で終わる）を送る。改行の前に \r を足す
puts:
    push %rax
    push %rsi
4:  lodsb
    test %al, %al
    jz 6f
    cmp $'\n', %al
    jne 5f
    mov $'\r', %al
    call putc
    mov $'\n', %al
5:  call putc
    jmp 4b
6:  pop %rsi
    pop %rax
    ret

# read_line: rdi に 1 行読む（最大 255 文字、0 で終える）。打った文字はその場で表示し、バックスペースで消す。長さを rax に返す
read_line:
    push %rbx
    push %rsi
    xor %ebx, %ebx                      # rbx = 今の長さ
7:  call getc
    cmp $'\r', %al
    je 9f
    cmp $'\n', %al
    je 9f
    cmp $0x7f, %al                      # DEL（多くの端末のバックスペース）
    je 8f
    cmp $0x08, %al                      # BS
    je 8f
    cmp $255, %rbx
    jae 7b                              # いっぱいなら捨てる
    mov %al, (%rdi,%rbx)
    inc %rbx
    call putc
    jmp 7b
8:  test %rbx, %rbx
    jz 7b
    dec %rbx
    mov $erase, %rsi                    # 1 文字戻って空白で消し、また戻る
    call puts
    jmp 7b
9:  movb $0, (%rdi,%rbx)
    mov $newline, %rsi
    call puts
    mov %rbx, %rax
    pop %rsi
    pop %rbx
    ret

    .section .rodata
banner:  .asciz "\nminios tiny\n"
prompt:  .asciz "> "
newline: .asciz "\n"
erase:   .asciz "\b \b"

    .section .bss
line:    .skip 256

    .section .note.GNU-stack, "", @progbits
