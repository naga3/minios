// PID 1. libc なし、システムコール直叩き。
#define SYS_read   0
#define SYS_write  1
#define SYS_ioctl  16
#define SYS_uname  63
#define SYS_reboot 169

#define TCGETS 0x5401
#define TCSETS 0x5402
#define ICANON 0000002
#define ECHO   0000010

struct termios {
    unsigned int c_iflag, c_oflag, c_cflag, c_lflag;
    unsigned char c_line;
    unsigned char c_cc[19];
};

struct utsname {
    char sysname[65], nodename[65], release[65], version[65], machine[65], domainname[65];
};

static long syscall4(long n, long a, long b, long c, long d) {
    long ret;
    register long r10 asm("r10") = d;
    asm volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

static void print(const char *s) {
    long n = 0;
    while (s[n]) n++;
    syscall4(SYS_write, 1, (long)s, n, 0);
}

void _start(void) {
    struct utsname u;
    syscall4(SYS_uname, (long)&u, 0, 0, 0);

    print("\033[2J\033[H");  // 画面クリア + カーソルを左上へ
    print("\n  \033[1;32mminios\033[0m 0.1\n");
    print("  kernel: "); print(u.sysname); print(" "); print(u.release); print(" "); print(u.machine); print("\n\n");
    print("  [ \033[32mOK\033[0m ] Started PID 1\n");
    print("  [ \033[32mOK\033[0m ] Reached target minimal\n\n");
    print("  Press any key to shut down...");

    // raw モード：Enter を待たず 1 文字で read が返るようにし、エコーも止める
    struct termios t;
    syscall4(SYS_ioctl, 0, TCGETS, (long)&t, 0);
    t.c_lflag &= ~(ICANON | ECHO);
    syscall4(SYS_ioctl, 0, TCSETS, (long)&t, 0);

    char c;
    syscall4(SYS_read, 0, (long)&c, 1, 0);
    print("\n\n  Bye.\n\n");

    // LINUX_REBOOT_CMD_RESTART。ACPI が無いので電源断はできない。QEMU の -no-reboot で終了させる
    syscall4(SYS_reboot, 0xfee1dead, 672274793, 0x01234567, 0);
    for (;;) {}
}
