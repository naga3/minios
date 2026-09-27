// スタートアップ（init と外部プログラムで共通）。_start でスタックから argc・argv・envp を取り出し、
// スタックを 16 バイト境界に揃えてから main を呼んで exit する
#pragma once
#include "lib.h"

int main(int argc, char **argv, char **envp);

static void __attribute__((noreturn)) exit(int code) {
    syscall3(SYS_exit_group, code, 0, 0);
    for (;;) {}
}

// カーネルは起動直後のスタックに [argc][argv...][NULL][envp...][NULL] を積んでいる
__attribute__((used)) static void start_c(long *sp) {
    int argc = sp[0];
    char **argv = (char **)(sp + 1);
    exit(main(argc, argv, argv + argc + 1));
}

asm(".global _start\n"
    "_start:\n"
    "  xor %rbp, %rbp\n"
    "  mov %rsp, %rdi\n"
    "  and $-16, %rsp\n"
    "  call start_c\n");
