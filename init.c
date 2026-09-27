// minios の PID 1。起動メッセージを出し、コマンドを受け付ける
#include "lib.h"

#define VERSION "0.2"

static void banner(void) {
    struct utsname u;
    syscall3(SYS_uname, &u, 0, 0);
    print("\033[2J\033[H");  // 画面クリア + カーソルを左上へ
    print("\n  \033[1;32mminios\033[0m " VERSION "\n");
    print("  kernel: "); print(u.sysname); print(" "); print(u.release); print(" "); print(u.machine); print("\n\n");
    print("  Type 'help' for commands.\n\n");
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
