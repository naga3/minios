# C から OS の関数を呼ぶための入口と、C のコンパイラが使う memcpy・memset
#
# C の呼び方（System V）: 引数は rdi, rsi, rdx, rcx, r8 の順、返り値は rax。rbx・rbp・r12〜r15 は呼ばれた側が守る。
# グラフィックの関数（fill_rect、draw_sprite、present、wait_vsync）と getc_nowait・sleep_ticks・con_clear は、
# もともと同じ並びで、使ったレジスタも戻すので、C からそのまま呼べる。文字の関数だけ、ここで並べ替える
    .code64
    .text
    .global memcpy, memset, text_color, text_goto, text_write, text_puts, text_cursor

# void *memcpy(void *to, const void *from, unsigned long n)
memcpy:
    mov %rdi, %rax
    mov %rdx, %rcx
    rep movsb
    ret

# void *memset(void *to, int c, unsigned long n)
memset:
    mov %rdi, %r8
    mov %esi, %eax
    mov %rdx, %rcx
    rep stosb
    mov %r8, %rax
    ret

# void text_color(int fg, int bg)
text_color:
    mov %esi, %eax
    shl $8, %eax
    mov %dil, %al
    jmp con_color

# void text_goto(int col, int row)
text_goto:
    mov %esi, %eax
    shl $8, %eax
    mov %dil, %al
    jmp con_goto

# void text_write(const char *s): 画面へは写さない
text_write:
    mov %rdi, %rsi
    jmp con_write

# void text_puts(const char *s): 書いて画面へ写す
text_puts:
    mov %rdi, %rsi
    jmp con_puts

# void text_cursor(int on)
text_cursor:
    mov %edi, %eax
    jmp con_cursor

    .section .note.GNU-stack, "", @progbits
