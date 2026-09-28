// システムコール。番号と引数の並びは Linux（x86_64）と同じにする。
// Linux 版の minios のプログラムを、そのまま動かすため
#include "kernel.h"

extern void syscall_entry(void);

#define ENOSYS 38

static void wrmsr(u32 msr, u64 v) { __asm__ volatile("wrmsr" ::"c"(msr), "a"((u32)v), "d"((u32)(v >> 32))); }
static u64 rdmsr(u32 msr) {
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (u64)hi << 32 | lo;
}

void syscall_init(void) {
    wrmsr(0xC0000080, rdmsr(0xC0000080) | 1);                      // EFER.SCE: syscall 命令を使えるように
    wrmsr(0xC0000081, (u64)0x10 << 48 | (u64)0x08 << 32);          // STAR: カーネル 0x08、ユーザーの基準 0x10
    wrmsr(0xC0000082, (u64)syscall_entry);                         // LSTAR: 入り口
    wrmsr(0xC0000084, 0x200 | 0x400);                              // SFMASK: 入るときに割り込みと方向フラグを落とす
}

static long sys_write(long fd, u64 buf, u64 n) {
    if (fd != 1 && fd != 2) return -9;  // EBADF（今はシリアルだけ）
    for (u64 i = 0; i < n; i++) {
        char *c = user_ptr(cur->pml4, buf + i);
        if (!c) return -14;             // EFAULT
        if (*c == '\n') serial_putc('\r');
        serial_putc(*c);
    }
    return n;
}

void syscall_dispatch(struct regs *r) {
    long ret = -ENOSYS;
    switch (r->rax) {
    case 1:   ret = sys_write(r->rdi, r->rsi, r->rdx); break;
    case 35:  ret = sys_nanosleep(r->rdi); break;
    case 39:  ret = cur->pid; break;                           // getpid
    case 57:  ret = sys_fork(r); break;
    case 59:                                                  // execve: 成功したら r は新しいプログラムの状態
        ret = sys_execve(r);
        if (ret == 0) return;
        break;
    case 61:  ret = sys_wait4(r->rdi, r->rsi, r->rdx); break;
    case 110: ret = cur->ppid; break;                         // getppid
    case 60:                                                  // exit
    case 231:                                                 // exit_group
        process_exit(r->rdi);
        break;
    default:
        kprint("[syscall "); kprint_u(r->rax); kprint(" not implemented]\n");
    }
    r->rax = ret;
}