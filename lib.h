// libc の代わり。システムコールと、最低限の文字列・数値処理
#pragma once

typedef unsigned long size_t;

#define SYS_read       0
#define SYS_write      1
#define SYS_open       2
#define SYS_close      3
#define SYS_stat       4
#define SYS_lstat      6
#define SYS_rt_sigaction 13
#define SYS_ioctl      16
#define SYS_pipe       22
#define SYS_nanosleep  35
#define SYS_dup        32
#define SYS_dup2       33
#define SYS_getpid     39
#define SYS_fork       57
#define SYS_execve     59
#define SYS_wait4      61
#define SYS_uname      63
#define SYS_getcwd     79
#define SYS_chdir      80
#define SYS_mkdir      83
#define SYS_rmdir      84
#define SYS_chmod      90
#define SYS_unlink     87
#define SYS_sysinfo    99
#define SYS_syslog     103
#define SYS_statfs     137
#define SYS_sync       162
#define SYS_mount      165
#define SYS_umount2    166
#define SYS_reboot     169
#define SYS_setpgid    109
#define SYS_getppid    110
#define SYS_setsid     112
#define SYS_getdents64 217
#define SYS_clock_gettime 228
#define SYS_exit_group 231

#define O_RDONLY    0
#define O_WRONLY    01
#define O_RDWR      02
#define O_CREAT     0100
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_DIRECTORY 0200000

#define EISDIR 21

#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFLNK 0120000
#define S_IFCHR 0020000
#define S_IFBLK 0060000

struct stat {
    unsigned long st_dev, st_ino, st_nlink;
    unsigned int st_mode, st_uid, st_gid, __pad0;
    unsigned long st_rdev;
    long st_size, st_blksize, st_blocks;
    unsigned long st_atime, st_atime_nsec, st_mtime, st_mtime_nsec, st_ctime, st_ctime_nsec;
    long __unused[3];
};

static long syscall6(long n, long a, long b, long c, long d, long e, long f) {
    long ret;
    register long r10 asm("r10") = d;
    register long r8 asm("r8") = e;
    register long r9 asm("r9") = f;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                 : "rcx", "r11", "memory");
    return ret;
}
#define syscall3(n, a, b, c) syscall6(n, (long)(a), (long)(b), (long)(c), 0, 0, 0)

// ---- 端末 ----

#define TCGETS     0x5401
#define TCSETS     0x5402
#define TCSBRK     0x5409  // 引数 1 で tcdrain（送信バッファが空になるまで待つ）
#define TIOCSCTTY  0x540E
#define TIOCSPGRP  0x5410
#define VSUSP      10

struct termios {
    unsigned int c_iflag, c_oflag, c_cflag, c_lflag;
    unsigned char c_line;
    unsigned char c_cc[19];
};

// ---- シグナル ----

#define SIG_DFL 0
#define SIG_IGN 1
#define SIGINT  2
#define SIGQUIT 3
#define SIGTTOU 22

struct kernel_sigaction {
    unsigned long handler, flags, restorer, mask;
};

static long signal(int sig, unsigned long handler) {
    struct kernel_sigaction sa = {.handler = handler};
    return syscall6(SYS_rt_sigaction, sig, (long)&sa, 0, sizeof sa.mask, 0, 0);
}

// ---- 文字列 ----

static size_t slen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int seq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

// buf（大きさ size、今 *n 文字）の後ろに s をつなげて NUL 終端する。入りきらない分は捨てる
static void str_append(char *buf, size_t size, size_t *n, const char *s) {
    while (*s && *n < size - 1) buf[(*n)++] = *s++;
    buf[*n] = 0;
}

// 先頭の空白を飛ばし、次の空白で NUL 終端して単語を切り出す。*s は単語の次へ進む
static char *next_word(char **s) {
    char *p = *s;
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return 0;
    char *w = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (*p) *p++ = 0;
    *s = p;
    return w;
}

static int is_digits(const char *s) {
    if (!*s) return 0;
    for (; *s; s++)
        if (*s < '0' || *s > '9') return 0;
    return 1;
}

// ---- 出力 ----

static void write_all(int fd, const char *s, size_t n) {
    while (n > 0) {
        long r = syscall3(SYS_write, fd, s, n);
        if (r <= 0) return;
        s += r, n -= r;
    }
}

static void print(const char *s) { write_all(1, s, slen(s)); }

// 文字列の定数を書く。長さはコンパイラが数える（手で数えて 1 文字ずれたことがある。周 26）
#define write_lit(fd, s) write_all(fd, s, sizeof(s) - 1)

static void print_u(unsigned long v) {
    char b[21];
    int i = sizeof b;
    b[--i] = 0;
    do b[--i] = '0' + v % 10; while (v /= 10);
    print(b + i);
}

// 右寄せで width 桁
static void print_u_pad(unsigned long v, int width) {
    unsigned long t = v;
    int n = 1;
    while (t /= 10) n++;
    while (n++ < width) print(" ");
    print_u(v);
}

static const char *strerror(long err) {
    switch (-err) {
    case 1:  return "operation not permitted";
    case 2:  return "no such file or directory";
    case 5:  return "I/O error";
    case 9:  return "bad file descriptor";
    case 12: return "out of memory";
    case 13: return "permission denied";
    case 16: return "busy";
    case 17: return "already exists";
    case 20: return "not a directory";
    case 21: return "is a directory";
    case 22: return "invalid argument";
    case 28: return "no space left";
    case 36: return "name too long";
    case 38: return "not implemented (kernel option?)";
    case 39: return "directory not empty";
    case 40: return "too many levels (loop?)";
    case 110: return "timed out";
    case 116: return "stale file handle (filesystem damaged?)";
    }
    return 0;
}

// エラーは標準エラー（fd 2）へ。リダイレクト中でも画面に出る
static void print_err(const char *what, long err) {
    const char *msg = strerror(err);
    write_all(2, what, slen(what));
    write_lit(2, ": ");
    if (msg) {
        write_all(2, msg, slen(msg));
    } else {
        write_lit(2, "error ");
        char b[21];
        int i = sizeof b;
        unsigned long v = -err;
        do b[--i] = '0' + v % 10; while (v /= 10);
        write_all(2, b + i, sizeof b - i);
    }
    write_lit(2, "\n");
}

// ---- ファイル ----

// dir + "/" + name を out に組み立てる（dir が "/" や空なら重ねない）
static void join_path(char *out, size_t size, const char *dir, const char *name) {
    size_t n = 0;
    for (const char *p = dir; *p && n < size - 1; p++) out[n++] = *p;
    if (n > 0 && out[n - 1] != '/' && n < size - 1) out[n++] = '/';
    for (const char *p = name; *p && n < size - 1; p++) out[n++] = *p;
    out[n] = 0;
}

// path を最大 size-1 バイト読んで NUL 終端する。読めたバイト数か、負のエラー番号を返す
static long read_file(const char *path, char *buf, size_t size) {
    long fd = syscall3(SYS_open, path, O_RDONLY, 0);
    if (fd < 0) return fd;
    size_t n = 0;
    while (n < size - 1) {
        long r = syscall3(SYS_read, fd, buf + n, size - 1 - n);
        if (r <= 0) break;
        n += r;
    }
    syscall3(SYS_close, fd, 0, 0);
    buf[n] = 0;
    return n;
}

struct linux_dirent64 {
    unsigned long d_ino;
    long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

// ディレクトリの各エントリ名で fn を呼ぶ。0 か負のエラー番号を返す
static long each_dirent(const char *path, void (*fn)(const char *name, unsigned char type, void *ctx), void *ctx) {
    long fd = syscall3(SYS_open, path, O_RDONLY | O_DIRECTORY, 0);
    if (fd < 0) return fd;
    char buf[1024] __attribute__((aligned(8)));
    long n;
    while ((n = syscall3(SYS_getdents64, fd, buf, sizeof buf)) > 0) {
        for (long off = 0; off < n;) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + off);
            fn(d->d_name, d->d_type, ctx);
            off += d->d_reclen;
        }
    }
    syscall3(SYS_close, fd, 0, 0);
    return n < 0 ? n : 0;
}

// ---- システム情報 ----

struct utsname {
    char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
};

struct sysinfo {
    long uptime;
    unsigned long loads[3];
    unsigned long totalram, freeram, sharedram, bufferram, totalswap, freeswap;
    unsigned short procs, pad;
    unsigned long totalhigh, freehigh;
    unsigned int mem_unit;
    char _f[20 - 2 * sizeof(long) - sizeof(int)];
};

struct statfs {
    long f_type, f_bsize;
    unsigned long f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int f_fsid[2];
    long f_namelen, f_frsize, f_flags, f_spare[4];
};

// ---- 電源 ----

#define REBOOT_MAGIC1 0xfee1dead
#define REBOOT_MAGIC2 672274793
#define REBOOT_CMD_RESTART  0x01234567
#define REBOOT_CMD_POWER_OFF 0x4321fedc

// ACPI があれば電源を切る。無ければ（テスト用の最小カーネル）電源断ができないので再起動し、QEMU の -no-reboot で終了させる
static void __attribute__((noreturn)) power_off(void) {
    struct stat st;
    int acpi = syscall3(SYS_stat, "/sys/firmware/acpi", &st, 0) >= 0;
    syscall6(SYS_reboot, REBOOT_MAGIC1, REBOOT_MAGIC2, acpi ? REBOOT_CMD_POWER_OFF : REBOOT_CMD_RESTART, 0, 0, 0);
    for (;;) {}
}
