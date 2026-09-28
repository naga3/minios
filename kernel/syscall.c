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

long sys_ioctl(long fd, long cmd, u64 arg) {
    if (fd < 0 || fd >= NFD || !cur->fds[fd]) return -9;
    if (cur->fds[fd]->kind != F_TTY) return -25;  // ENOTTY
    return tty_ioctl(cmd, arg);
}

static long sys_prctl(long option, u64 arg) {
    if (option == 15) {  // PR_SET_NAME
        char name[16];
        long n = copy_str_in(name, arg, sizeof name);
        if (n < 0 && n != -36) return n;
        name[15] = 0;
        set_name(cur, name);
        return 0;
    }
    return -22;
}

void syscall_dispatch(struct regs *r) {
    long ret = -ENOSYS;
    long a = r->rdi, b = r->rsi, c = r->rdx;
    switch (r->rax) {
    case 0:   ret = sys_read(a, b, c); break;
    case 1:   ret = sys_write(a, b, c); break;
    case 2:   ret = sys_open(a, b, c); break;
    case 3:   ret = sys_close(a); break;
    case 4:   ret = sys_stat(a, b, 1); break;
    case 5:   ret = sys_fstat(a, b); break;
    case 6:   ret = sys_stat(a, b, 0); break;            // lstat
    case 8:   ret = sys_lseek(a, b, c); break;
    case 13:  ret = sys_rt_sigaction(a, b, c); break;
    case 16:  ret = sys_ioctl(a, b, c); break;
    case 22:  ret = sys_pipe(a); break;
    case 32:  ret = sys_dup(a); break;
    case 33:  ret = sys_dup2(a, b); break;
    case 35:  ret = sys_nanosleep(a); break;
    case 39:  ret = cur->pid; break;                     // getpid
    case 57:  ret = sys_fork(r); break;
    case 59:                                             // execve: 成功したら r は新しいプログラムの状態
        ret = sys_execve(r);
        if (ret == 0) { signal_check(); return; }
        break;
    case 60:                                             // exit
    case 231:                                            // exit_group
        process_exit(a);
        break;
    case 61:  ret = sys_wait4(a, b, c); break;
    case 62:  ret = sys_kill(a, b); break;
    case 79:  ret = sys_getcwd(a, b); break;
    case 80:  ret = sys_chdir(a); break;
    case 83:  ret = sys_mkdir(a, b); break;
    case 84:  ret = sys_rmdir(a); break;
    case 87:  ret = sys_unlink(a); break;
    case 90:  ret = sys_chmod(a, b); break;
    case 109: ret = sys_setpgid(a, b); break;
    case 110: ret = cur->ppid; break;                    // getppid
    case 112: ret = sys_setsid(); break;
    case 157: ret = sys_prctl(a, b); break;
    case 217: ret = sys_getdents64(a, b, c); break;
    default:
        kprint("[syscall "); kprint_u(r->rax); kprint(" not implemented]\n");
    }
    r->rax = ret;
    signal_check();
}