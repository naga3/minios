// スタートアップ（init と外部プログラムで共通）。_start でスタックから argc・argv・envp を取り出し、
// スタックを 16 バイト境界に揃えてから main を呼んで exit する
#pragma once
#include "lib.h"

int main(int argc, char **argv, char **envp);

// コンパイラは構造体の初期化やコピーで memset・memcpy などを呼ぶことがある（gcc も TinyCC も）。libc が無いので自前で用意する。
// gcc はこの中のループを memset の呼び出しに置き換えてしまう（自分を呼び続ける）ので、その最適化を切る
#define NO_LIBCALL __attribute__((weak, optimize("no-tree-loop-distribute-patterns")))

NO_LIBCALL void *memset(void *d, int c, size_t n) {
    unsigned char *p = d;
    while (n--) *p++ = c;
    return d;
}

NO_LIBCALL void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *p = d;
    const unsigned char *q = s;
    while (n--) *p++ = *q++;
    return d;
}

NO_LIBCALL void *memmove(void *d, const void *s, size_t n) {
    unsigned char *p = d;
    const unsigned char *q = s;
    if (p < q) {
        while (n--) *p++ = *q++;
    } else {
        while (n--) p[n] = q[n];
    }
    return d;
}

NO_LIBCALL int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *p = a, *q = b;
    for (; n--; p++, q++)
        if (*p != *q) return *p - *q;
    return 0;
}

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
