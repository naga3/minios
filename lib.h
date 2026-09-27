// libc の代わり。システムコールと、最低限の文字列・数値処理
#pragma once

typedef unsigned long size_t;

#define SYS_read    0
#define SYS_write   1
#define SYS_ioctl   16
#define SYS_sysinfo 99
#define SYS_uname   63
#define SYS_reboot  169

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

// ---- 出力 ----

static void write_all(int fd, const char *s, size_t n) {
    while (n > 0) {
        long r = syscall3(SYS_write, fd, s, n);
        if (r <= 0) return;
        s += r, n -= r;
    }
}

static void print(const char *s) { write_all(1, s, slen(s)); }

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

// ---- 電源 ----

#define REBOOT_MAGIC1 0xfee1dead
#define REBOOT_MAGIC2 672274793
#define REBOOT_CMD_RESTART 0x01234567

// ACPI が無いので電源断はできない。再起動し、QEMU の -no-reboot で終了させる
static void __attribute__((noreturn)) restart(void) {
    syscall6(SYS_reboot, REBOOT_MAGIC1, REBOOT_MAGIC2, REBOOT_CMD_RESTART, 0, 0, 0);
    for (;;) {}
}
