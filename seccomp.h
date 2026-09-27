// seccomp でプログラムが使えるシステムコールを制限する（grow で入れたプログラムの隔離）。
// 許すシステムコールを名前の並び（"write exit_group" や "write,exit_group"）で受け取り、それ以外を呼んだらカーネルがプロセスを止める
#pragma once
#include "lib.h"

#define SYS_prctl_  157
#define SYS_seccomp 317
#define PR_SET_NO_NEW_PRIVS 38
#define SECCOMP_SET_MODE_FILTER 1
#define SECCOMP_RET_KILL_PROCESS 0x80000000U
#define SECCOMP_RET_ALLOW        0x7fff0000U
#define AUDIT_ARCH_X86_64        0xc000003eU
#define SIGSYS 31

// 名前で指定できるシステムコール（x86_64 の番号）。知らない名前は拒否する（間違いで広く許さないように）
static const struct { const char *name; int nr; } syscall_names[] = {
    {"read", 0}, {"write", 1}, {"open", 2}, {"close", 3}, {"stat", 4}, {"fstat", 5}, {"lstat", 6},
    {"lseek", 8}, {"mmap", 9}, {"munmap", 11}, {"brk", 12}, {"rt_sigaction", 13}, {"ioctl", 16},
    {"pipe", 22}, {"dup", 32}, {"dup2", 33}, {"nanosleep", 35}, {"getpid", 39}, {"socket", 41},
    {"connect", 42}, {"sendto", 44}, {"recvfrom", 45}, {"bind", 49}, {"setsockopt", 54}, {"fork", 57},
    {"execve", 59}, {"exit", 60}, {"wait4", 61}, {"uname", 63}, {"getcwd", 79}, {"chdir", 80},
    {"rename", 82}, {"mkdir", 83}, {"rmdir", 84}, {"unlink", 87}, {"chmod", 90}, {"sysinfo", 99},
    {"getppid", 110}, {"statfs", 137}, {"sync", 162}, {"getdents64", 217}, {"clock_gettime", 228},
    {"exit_group", 231},
};
#define NSYSCALL_NAMES (sizeof syscall_names / sizeof syscall_names[0])

static int syscall_nr(const char *name, size_t len) {
    for (size_t i = 0; i < NSYSCALL_NAMES; i++) {
        const char *n = syscall_names[i].name;
        if (slen(n) != len) continue;
        size_t k = 0;
        while (k < len && n[k] == name[k]) k++;
        if (k == len) return syscall_names[i].nr;
    }
    return -1;
}

struct sock_filter { unsigned short code; unsigned char jt, jf; unsigned int k; };
struct sock_fprog { unsigned short len; struct sock_filter *filter; };

#define BPF_LD_W_ABS 0x20  // A = 読み込み（seccomp_data の k バイト目から 4 バイト）
#define BPF_JEQ_K    0x15  // A == k なら jt 先へ、違えば jf 先へ
#define BPF_RET_K    0x06  // k を返す

// list（空白かコンマ区切りの名前）以外のシステムコールを禁じる。execve は許す（フィルタをかけた後に exec するため。
// exec した先にも同じフィルタが引き継がれるので、execve で制限を抜けることはできない）。
// 成功なら 0。知らない名前があれば、その名前を bad に入れて -22
static long apply_syscall_filter(const char *list, const char **bad, size_t *bad_len) {
    struct sock_filter f[2 + 2 * 64 + 2];
    int n = 0, allowed[64], nallowed = 0;
    allowed[nallowed++] = 59;  // execve
    for (const char *p = list; *p;) {
        while (*p == ' ' || *p == ',' || *p == '\t') p++;
        const char *w = p;
        while (*p && *p != ' ' && *p != ',' && *p != '\t' && *p != '\n') p++;
        if (p == w) { if (*p) p++; continue; }
        int nr = syscall_nr(w, p - w);
        if (nr < 0) { *bad = w; *bad_len = p - w; return -22; }
        if (nallowed < 64) allowed[nallowed++] = nr;
        if (*p == '\n') break;
    }
    // アーキテクチャが x86_64 でなければ止める（32 ビットの番号で呼んで抜けられないように）
    f[n++] = (struct sock_filter){BPF_LD_W_ABS, 0, 0, 4};                 // seccomp_data.arch
    f[n++] = (struct sock_filter){BPF_JEQ_K, 1, 0, AUDIT_ARCH_X86_64};
    f[n++] = (struct sock_filter){BPF_RET_K, 0, 0, SECCOMP_RET_KILL_PROCESS};
    f[n++] = (struct sock_filter){BPF_LD_W_ABS, 0, 0, 0};                 // seccomp_data.nr
    for (int i = 0; i < nallowed; i++) {
        f[n++] = (struct sock_filter){BPF_JEQ_K, 0, 1, (unsigned)allowed[i]};
        f[n++] = (struct sock_filter){BPF_RET_K, 0, 0, SECCOMP_RET_ALLOW};
    }
    f[n++] = (struct sock_filter){BPF_RET_K, 0, 0, SECCOMP_RET_KILL_PROCESS};
    struct sock_fprog prog = {n, f};
    long e = syscall6(SYS_prctl_, PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0, 0);
    if (e < 0) return e;
    return syscall3(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog);
}
