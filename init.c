// minios の PID 1。起動メッセージを出し、コマンドを受け付ける
#include "lib.h"

#define VERSION "0.3"

static void banner(void) {
    struct utsname u;
    syscall3(SYS_uname, &u, 0, 0);
    print("\033[2J\033[H");  // 画面クリア + カーソルを左上へ
    print("\n  \033[1;32mminios\033[0m " VERSION "\n");
    print("  kernel: "); print(u.sysname); print(" "); print(u.release); print(" "); print(u.machine); print("\n\n");
    print("  Type 'help' for commands.\n\n");
}

// 起動時にカーネルの疑似ファイルシステムをマウントする
static void mount_all(void) {
    static const struct { const char *src, *dir, *type; } mounts[] = {
        {"proc",  "/proc", "proc"},
        {"sysfs", "/sys",  "sysfs"},
    };
    for (size_t i = 0; i < sizeof mounts / sizeof mounts[0]; i++) {
        long r = syscall6(SYS_mount, (long)mounts[i].src, (long)mounts[i].dir, (long)mounts[i].type, 0, 0, 0);
        if (r < 0) print_err(mounts[i].dir, r);
    }
}

// ---- コマンド ----

static void cmd_help(char *args);

static void cmd_uname(char *args) {
    (void)args;
    struct utsname u;
    syscall3(SYS_uname, &u, 0, 0);
    print(u.sysname); print(" "); print(u.release); print(" "); print(u.version); print(" "); print(u.machine); print("\n");
}

static void cmd_uptime(char *args) {
    (void)args;
    struct sysinfo si;
    syscall3(SYS_sysinfo, &si, 0, 0);
    print("up "); print_u(si.uptime); print("s\n");
}

static void cmd_mem(char *args) {
    (void)args;
    struct sysinfo si;
    syscall3(SYS_sysinfo, &si, 0, 0);
    unsigned long unit = si.mem_unit ? si.mem_unit : 1;
    print("total "); print_u_pad(si.totalram * unit / 1024, 8); print(" KiB\n");
    print("free  "); print_u_pad(si.freeram * unit / 1024, 8); print(" KiB\n");
}

// /proc/<pid>/stat の "pid (comm) state ..." から 1 行出す
static void ps_entry(const char *name, unsigned char type, void *ctx) {
    (void)type, (void)ctx;
    if (!is_digits(name)) return;
    char path[64] = "/proc/", buf[512];
    size_t n = slen(path);
    for (const char *p = name; *p; p++) path[n++] = *p;
    for (const char *p = "/stat"; *p; p++) path[n++] = *p;
    path[n] = 0;
    if (read_file(path, buf, sizeof buf) <= 0) return;
    // comm は空白や ')' を含み得るので、最後の ')' で区切る
    char *open = buf, *close = 0;
    while (*open && *open != '(') open++;
    for (char *p = buf; *p; p++)
        if (*p == ')') close = p;
    if (!*open || !close) return;
    *close = 0;
    print("  ");
    for (size_t pad = slen(name); pad < 5; pad++) print(" ");
    print(name); print("  "); write_all(1, close + 2, 1); print("  "); print(open + 1); print("\n");
}

static void cmd_ps(char *args) {
    (void)args;
    print("    PID  S  COMMAND\n");
    long r = each_dirent("/proc", ps_entry, 0);
    if (r < 0) print_err("/proc", r);
}

#define SYSLOG_ACTION_READ_ALL    3
#define SYSLOG_ACTION_SIZE_BUFFER 10

static void cmd_dmesg(char *args) {
    (void)args;
    static char buf[1 << 17];
    long size = syscall3(SYS_syslog, SYSLOG_ACTION_SIZE_BUFFER, 0, 0);
    if (size < 0) { print_err("dmesg", size); return; }
    if (size > (long)sizeof buf) size = sizeof buf;
    long n = syscall3(SYS_syslog, SYSLOG_ACTION_READ_ALL, buf, size);
    if (n < 0) { print_err("dmesg", n); return; }
    // 各行の先頭の "<6>" のような優先度を落として出す
    for (long i = 0; i < n;) {
        if (buf[i] == '<') {
            long j = i + 1;
            while (j < n && buf[j] >= '0' && buf[j] <= '9') j++;
            if (j < n && buf[j] == '>') i = j + 1;
        }
        long start = i;
        while (i < n && buf[i] != '\n') i++;
        if (i < n) i++;
        write_all(1, buf + start, i - start);
    }
}

static void cmd_mounts(char *args) {
    (void)args;
    static char buf[4096];
    long n = read_file("/proc/mounts", buf, sizeof buf);
    if (n < 0) { print_err("/proc/mounts", n); return; }
    print(buf);
}

static void cmd_echo(char *args) {
    char *w;
    int first = 1;
    while ((w = next_word(&args))) {
        if (!first) print(" ");
        print(w);
        first = 0;
    }
    print("\n");
}

static void cmd_clear(char *args) {
    (void)args;
    print("\033[2J\033[H");
}

static void cmd_shutdown(char *args) {
    (void)args;
    print("Bye.\n");
    restart();
}

static const struct {
    const char *name;
    void (*fn)(char *args);
    const char *help;
} commands[] = {
    {"help",     cmd_help,     "show this help"},
    {"uname",    cmd_uname,    "kernel name and version"},
    {"uptime",   cmd_uptime,   "seconds since boot"},
    {"mem",      cmd_mem,      "memory usage"},
    {"ps",       cmd_ps,       "list processes"},
    {"dmesg",    cmd_dmesg,    "kernel log"},
    {"mounts",   cmd_mounts,   "mounted filesystems"},
    {"echo",     cmd_echo,     "print arguments"},
    {"clear",    cmd_clear,    "clear the screen"},
    {"shutdown", cmd_shutdown, "power off (exit QEMU)"},
};
#define NCOMMANDS (sizeof commands / sizeof commands[0])

static void cmd_help(char *args) {
    (void)args;
    for (size_t i = 0; i < NCOMMANDS; i++) {
        print("  "); print(commands[i].name);
        for (size_t n = slen(commands[i].name); n < 10; n++) print(" ");
        print(commands[i].help); print("\n");
    }
}

static void run(char *line) {
    char *cmd = next_word(&line);
    if (!cmd) return;
    for (size_t i = 0; i < NCOMMANDS; i++) {
        if (seq(cmd, commands[i].name)) {
            commands[i].fn(line);
            return;
        }
    }
    print(cmd); print(": command not found\n");
}

void _start(void) {
    mount_all();
    banner();

    // 行編集（エコー、バックスペース）はカーネルの端末（canonical モード）に任せる
    char line[256];
    for (;;) {
        print("minios# ");
        long n = syscall3(SYS_read, 0, line, sizeof line - 1);
        if (n <= 0) continue;
        line[n] = 0;
        if (line[n - 1] == '\n') line[n - 1] = 0;
        run(line);
    }
}
