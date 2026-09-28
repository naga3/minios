// syscall 命令の入り口。ユーザーのスタックからカーネルのスタックに切り替え、レジスタを struct regs の形で積んで
// syscall_dispatch（C）を呼び、sysretq で戻る。rcx にユーザーの rip、r11 に rflags が入っている
    .text
    .code64
    .global syscall_entry
syscall_entry:
    mov %rsp, user_rsp(%rip)
    mov kernel_rsp(%rip), %rsp
    push $0x1b                       // struct regs の ss（ユーザーのデータ）
    push user_rsp(%rip)              // rsp
    push %r11                        // rflags
    push $0x23                       // cs（ユーザーのコード）
    push %rcx                        // rip
    push $0                          // error
    push $0                          // vector
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, %rdi
    sti                              // システムコールの最中も割り込みを受ける
    call syscall_dispatch
    cli
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    add $16, %rsp                    // vector と error
    pop %rcx                         // rip（戻り先。exec などで書き換わっていることがある）
    add $8, %rsp                     // cs
    pop %r11                         // rflags
    pop %rsp                         // ユーザーのスタック
    sysretq

// ユーザーモードに入る（iretq）。rdi = struct regs *（rip・rsp などが入ったもの）
    .global enter_user
enter_user:
    mov %rdi, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    add $16, %rsp
    iretq

    .data
    .global kernel_rsp
kernel_rsp: .quad 0
user_rsp:   .quad 0

    .section .note.GNU-stack, "", @progbits
