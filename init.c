// minios の PID 1。起動メッセージを出し、コマンドを受け付ける
#include "lib.h"

#define VERSION "0.4"

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

// ---- ファイル操作 ----

struct ls_ctx { const char *dir; };

static void ls_entry(const char *name, unsigned char type, void *vctx) {
    (void)type;
    struct ls_ctx *ctx = vctx;
    if (name[0] == '.') return;
    char path[512];
    struct stat st;
    join_path(path, sizeof path, ctx->dir, name);
    if (syscall3(SYS_lstat, path, &st, 0) < 0) return;
    char kind = '-';
    switch (st.st_mode & S_IFMT) {
    case S_IFDIR: kind = 'd'; break;
    case S_IFLNK: kind = 'l'; break;
    case S_IFCHR: kind = 'c'; break;
    case S_IFBLK: kind = 'b'; break;
    }
    write_all(1, &kind, 1);
    print_u_pad(st.st_size, 9); print("  "); print(name);
    if (kind == 'd') print("/");
    print("\n");
}

static void cmd_ls(char *args) {
    char *dir = next_word(&args);
    struct ls_ctx ctx = {dir ? dir : "."};
    long r = each_dirent(ctx.dir, ls_entry, &ctx);
    if (r < 0) print_err(ctx.dir, r);
}

static void cmd_cat(char *args) {
    char *path;
    while ((path = next_word(&args))) {
        long fd = syscall3(SYS_open, path, O_RDONLY, 0);
        if (fd < 0) { print_err(path, fd); continue; }
        char buf[4096];
        long n;
        while ((n = syscall3(SYS_read, fd, buf, sizeof buf)) > 0) write_all(1, buf, n);
        if (n < 0) print_err(path, n);
        syscall3(SYS_close, fd, 0, 0);
    }
}

static void cmd_mkdir(char *args) {
    char *path;
    while ((path = next_word(&args))) {
        long r = syscall3(SYS_mkdir, path, 0755, 0);
        if (r < 0) print_err(path, r);
    }
}

static void cmd_rm(char *args) {
    char *path;
    while ((path = next_word(&args))) {
        long r = syscall3(SYS_unlink, path, 0, 0);
        if (r == -EISDIR) r = syscall3(SYS_rmdir, path, 0, 0);
        if (r < 0) print_err(path, r);
    }
}

static void cmd_cd(char *args) {
    char *path = next_word(&args);
    long r = syscall3(SYS_chdir, path ? path : "/", 0, 0);
    if (r < 0) print_err(path, r);
}

static void cmd_pwd(char *args) {
    (void)args;
    char buf[512];
    long r = syscall3(SYS_getcwd, buf, sizeof buf, 0);
    if (r < 0) { print_err("pwd", r); return; }
    print(buf); print("\n");
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
    {"ls",       cmd_ls,       "list directory"},
    {"cat",      cmd_cat,      "print files"},
    {"mkdir",    cmd_mkdir,    "make directories"},
    {"rm",       cmd_rm,       "remove files or empty directories"},
    {"cd",       cmd_cd,       "change directory"},
    {"pwd",      cmd_pwd,      "print working directory"},
    {"echo",     cmd_echo,     "print arguments (cmd > file, >> to append)"},
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

static void dispatch(char *line) {
    char *cmd = next_word(&line);
    if (!cmd) return;
    for (size_t i = 0; i < NCOMMANDS; i++) {
        if (seq(cmd, commands[i].name)) {
            commands[i].fn(line);
            return;
        }
    }
    write_all(2, cmd, slen(cmd));
    write_all(2, ": command not found\n", 20);
}

// "cmd args > file" / ">> file" なら、実行中だけ fd 1 をファイルに差し替える
static void run(char *line) {
    char *gt = 0;
    for (char *p = line; *p; p++)
        if (*p == '>') { gt = p; break; }
    if (!gt) { dispatch(line); return; }

    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    *gt = 0;
    char *rest = gt + 1;
    if (*rest == '>') { flags = O_WRONLY | O_CREAT | O_APPEND; rest++; }
    char *path = next_word(&rest);
    if (!path) { print_err(">", -22); return; }

    long fd = syscall3(SYS_open, path, flags, 0644);
    if (fd < 0) { print_err(path, fd); return; }
    long saved = syscall3(SYS_dup, 1, 0, 0);
    syscall3(SYS_dup2, fd, 1, 0);
    syscall3(SYS_close, fd, 0, 0);
    dispatch(line);
    syscall3(SYS_dup2, saved, 1, 0);
    syscall3(SYS_close, saved, 0, 0);
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
