// カーネルスタックの切り替え。void swtch(u64 *old_rsp, u64 new_rsp)
// 呼び出し先が保存すべきレジスタを今のスタックに積んで rsp を *old_rsp に取っておき、new_rsp のスタックから戻す
    .text
    .code64
    .global swtch
swtch:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, (%rdi)
    mov %rsi, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret

// 新しいプロセスが最初に swtch で切り替えられたとき、ここに戻る。r12 に struct regs * が入っている
    .global proc_trampoline
proc_trampoline:
    mov %r12, %rdi
    jmp enter_user

    .section .note.GNU-stack, "", @progbits
