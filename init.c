// minios の PID 1。起動メッセージを出し、コマンドを受け付ける
#include "lib.h"
#include "net.h"

#define VERSION "0.8"

static void banner(void) {
    struct utsname u;
    syscall3(SYS_uname, &u, 0, 0);
    print("\n  \033[1;32mminios\033[0m " VERSION "\n");
    print("  kernel: "); print(u.sysname); print(" "); print(u.release); print(" "); print(u.machine); print("\n\n");
}

static void status(const char *what, long err) {
    if (err < 0) {
        print("  [\033[31mFAIL\033[0m] "); print(what); print(": "); print(strerror(err) ? strerror(err) : "error"); print("\n");
    } else {
        print("  [ \033[32mOK\033[0m ] "); print(what); print("\n");
    }
}

static long mount(const char *src, const char *dir, const char *type) {
    return syscall6(SYS_mount, (long)src, (long)dir, (long)type, 0, 0, 0);
}

// 起動時のマウント。/data は永続ディスク（/dev/vda、minix）。無ければメモリだけで動く
static void mount_all(void) {
    status("Mounted /proc", mount("proc", "/proc", "proc"));
    status("Mounted /sys", mount("sysfs", "/sys", "sysfs"));
    status("Mounted /dev", mount("devtmpfs", "/dev", "devtmpfs"));
    struct stat st;
    if (syscall3(SYS_stat, "/dev/vda", &st, 0) < 0) {
        print("  [ -- ] No disk: /data is not persistent\n");
        return;
    }
    status("Mounted /data (/dev/vda)", mount("/dev/vda", "/data", "minix"));
}

// QEMU のユーザーモードネットワーク（slirp）の決まった値
#define NET_IFACE "eth0"
#define NET_ADDR  "10.0.2.15"
#define NET_MASK  "255.255.255.0"
#define NET_GW    "10.0.2.2"

static unsigned int ip(const char *s) {
    int ok;
    return parse_ip(s, &ok);
}

// lo と eth0 を上げ、eth0 に固定 IP とデフォルトルートを設定する
static void net_up(void) {
    long sock = syscall3(SYS_socket, AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { status("Network", sock); return; }
    long e = if_set_addr(sock, "lo", ip("127.0.0.1"), ip("255.0.0.0"));
    if (e >= 0) e = if_up(sock, "lo");
    status("Loopback lo 127.0.0.1", e);
    struct stat st;
    if (syscall3(SYS_stat, "/sys/class/net/" NET_IFACE, &st, 0) < 0) {
        print("  [ -- ] No network interface\n");
    } else {
        e = if_set_addr(sock, NET_IFACE, ip(NET_ADDR), ip(NET_MASK));
        if (e >= 0) e = if_up(sock, NET_IFACE);
        if (e >= 0) e = route_add_default(sock, ip(NET_GW));
        status("Network " NET_IFACE " " NET_ADDR " via " NET_GW, e);
    }
    syscall3(SYS_close, sock, 0, 0);
}

// 書き込み待ちのデータをディスクへ書き出し、/data を外す
static void unmount_all(void) {
    syscall3(SYS_sync, 0, 0, 0);
    syscall3(SYS_umount2, "/data", 0, 0);
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

// chmod <8 進数のモード> <パス>...
static void cmd_chmod(char *args) {
    char *mode_s = next_word(&args), *path;
    unsigned int mode = 0;
    for (char *p = mode_s ? mode_s : ""; *p; p++) {
        if (*p < '0' || *p > '7') { mode_s = 0; break; }
        mode = mode * 8 + (*p - '0');
    }
    if (!mode_s) { print_err("usage: chmod <octal-mode> <path>...", -22); return; }
    while ((path = next_word(&args))) {
        long r = syscall3(SYS_chmod, path, mode, 0);
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

static void cmd_sync(char *args) {
    (void)args;
    syscall3(SYS_sync, 0, 0, 0);
}

// /proc/mounts の各行（"src dir type opts ..."）について容量を出す
static void cmd_df(char *args) {
    (void)args;
    static char buf[4096];
    long n = read_file("/proc/mounts", buf, sizeof buf);
    if (n < 0) { print_err("/proc/mounts", n); return; }
    print("MOUNT      TYPE        SIZE KiB   USED KiB\n");
    char *line = buf;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n') end++;
        if (*end) *end++ = 0;
        char *rest = line;
        next_word(&rest);
        char *dir = next_word(&rest), *type = next_word(&rest);
        line = end;
        struct statfs f;
        if (!dir || !type || syscall3(SYS_statfs, dir, &f, 0) < 0 || f.f_blocks == 0) continue;
        unsigned long total = f.f_blocks * f.f_bsize / 1024, used = (f.f_blocks - f.f_bfree) * f.f_bsize / 1024;
        print(dir);
        for (size_t i = slen(dir); i < 11; i++) print(" ");
        print(type);
        for (size_t i = slen(type); i < 9; i++) print(" ");
        print_u_pad(total, 11); print_u_pad(used, 11); print("\n");
    }
}

// ---- ネットワーク ----

static void ifconfig_entry(const char *name, unsigned char type, void *ctx) {
    (void)type;
    long sock = *(long *)ctx;
    if (name[0] == '.') return;
    struct ifreq r;
    print(name); print(":");
    ifr_init(&r, name);
    if (syscall3(SYS_ioctl, sock, SIOCGIFFLAGS, &r) >= 0) print(r.flags & IFF_UP ? " UP" : " DOWN");
    ifr_init(&r, name);
    if (syscall3(SYS_ioctl, sock, SIOCGIFADDR, &r) >= 0) {
        print(" inet "); print_ip(((struct sockaddr_in *)&r.addr)->sin_addr);
        ifr_init(&r, name);
        if (syscall3(SYS_ioctl, sock, SIOCGIFNETMASK, &r) >= 0) {
            print(" mask "); print_ip(((struct sockaddr_in *)&r.addr)->sin_addr);
        }
    }
    ifr_init(&r, name);
    if (syscall3(SYS_ioctl, sock, SIOCGIFHWADDR, &r) >= 0 && !seq(name, "lo")) {
        static const char hex[] = "0123456789abcdef";
        print(" ether ");
        for (int i = 0; i < 6; i++) {
            unsigned char b = r.addr.sa_data[i];
            char h[3] = {hex[b >> 4], hex[b & 15], 0};
            if (i) print(":");
            print(h);
        }
    }
    print("\n");
}

static void cmd_ifconfig(char *args) {
    (void)args;
    long sock = syscall3(SYS_socket, AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { print_err("socket", sock); return; }
    long r = each_dirent("/sys/class/net", ifconfig_entry, &sock);
    if (r < 0) print_err("/sys/class/net", r);
    syscall3(SYS_close, sock, 0, 0);
}

struct icmp_echo {
    unsigned char type, code;
    unsigned short checksum, id, seq;
    char payload[32];
};

// ping <ip> [回数]
static void cmd_ping(char *args) {
    char *host = next_word(&args), *count_s = next_word(&args);
    int ok;
    unsigned int dst = host ? parse_ip(host, &ok) : 0;
    if (!host || !ok) { print_err("usage: ping <a.b.c.d> [count]", -22); return; }
    int count = 3;
    if (count_s && is_digits(count_s)) {
        count = 0;
        for (char *p = count_s; *p; p++) count = count * 10 + (*p - '0');
    }

    long sock = syscall3(SYS_socket, AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) { print_err("socket", sock); return; }
    struct timeval tv = {1, 0};
    syscall6(SYS_setsockopt, sock, SOL_SOCKET, SO_RCVTIMEO, (long)&tv, sizeof tv, 0);
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_addr = dst};
    unsigned short id = 0x6d69;  // "mi"
    int received = 0;

    for (int i = 1; i <= count; i++) {
        struct icmp_echo req = {.type = 8, .id = htons(id), .seq = htons(i)};
        for (size_t k = 0; k < sizeof req.payload; k++) req.payload[k] = 'a' + k % 26;
        req.checksum = inet_checksum(&req, sizeof req);
        unsigned long t0 = now_us();
        long e = syscall6(SYS_sendto, sock, (long)&req, sizeof req, 0, (long)&to, sizeof to);
        if (e < 0) { print_err("sendto", e); break; }
        // 自分宛ての返事（type 0、同じ id と seq）が来るまで読む
        for (;;) {
            unsigned char buf[128];
            long n = syscall6(SYS_recvfrom, sock, (long)buf, sizeof buf, 0, 0, 0);
            if (n == -EAGAIN) { print("timeout seq="); print_u(i); print("\n"); break; }
            if (n < 0) { print_err("recvfrom", n); break; }
            size_t ihl = (buf[0] & 0x0f) * 4;  // IP ヘッダの長さ
            if ((size_t)n < ihl + 8) continue;
            struct icmp_echo *rep = (struct icmp_echo *)(buf + ihl);
            if (rep->type != 0 || rep->id != htons(id) || rep->seq != htons(i)) continue;
            unsigned long us = now_us() - t0;
            print("reply from "); print_ip(dst); print(" seq="); print_u(i);
            print(" time="); print_u(us / 1000); print("."); print_u_pad(us % 1000 / 100, 1); print(" ms\n");
            received++;
            break;
        }
    }
    print_u(count); print(" sent, "); print_u(received); print(" received\n");
    syscall3(SYS_close, sock, 0, 0);
}

static void cmd_shutdown(char *args) {
    (void)args;
    unmount_all();
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
    {"chmod",    cmd_chmod,    "chmod <octal-mode> <path>..."},
    {"cd",       cmd_cd,       "change directory"},
    {"pwd",      cmd_pwd,      "print working directory"},
    {"df",       cmd_df,       "disk usage"},
    {"sync",     cmd_sync,     "flush writes to disk"},
    {"ifconfig", cmd_ifconfig, "network interfaces"},
    {"ping",     cmd_ping,     "ping <a.b.c.d> [count]"},
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
    print("\nOther commands run programs from /bin and /data/bin.\n");
}

// ---- 外部プログラム ----

static char *const child_env[] = {"HOME=/", "TERM=linux", "PATH=/bin:/data/bin", 0};
static const char *const path_dirs[] = {"/bin", "/data/bin"};

// cmd を実行ファイルのパスに解決する。"/" を含めばそのまま、無ければ path_dirs から探す
static int find_program(const char *cmd, char *out, size_t size) {
    struct stat st;
    for (const char *p = cmd; *p; p++) {
        if (*p == '/') {
            join_path(out, size, "", cmd);
            return syscall3(SYS_stat, out, &st, 0) >= 0;
        }
    }
    for (size_t i = 0; i < sizeof path_dirs / sizeof path_dirs[0]; i++) {
        join_path(out, size, path_dirs[i], cmd);
        if (syscall3(SYS_stat, out, &st, 0) >= 0 && (st.st_mode & S_IFMT) != S_IFDIR) return 1;
    }
    return 0;
}

// fork して子で execve し、終わるまで待つ。0 以外で終わったら終了コードかシグナルを出す
static void spawn(char *cmd, char *args) {
    char path[256];
    if (!find_program(cmd, path, sizeof path)) {
        write_all(2, cmd, slen(cmd));
        write_all(2, ": command not found\n", 20);
        return;
    }
    char *argv[32];
    int argc = 0;
    argv[argc++] = cmd;
    char *w;
    while (argc < 31 && (w = next_word(&args))) argv[argc++] = w;
    argv[argc] = 0;

    // 子を端末の前面に立たせるのは親（init）の仕事。親が init のプロセスグループは「孤児」扱いで、
    // 子が自分で TIOCSPGRP すると EIO になるため。切り替えが済むまで子をパイプで待たせる。
    // 前面を取り戻すとき init は背景にいるので、SIGTTOU を無視しておく（setup_tty）
    int sync_pipe[2];
    long e = syscall3(SYS_pipe, sync_pipe, 0, 0);
    if (e < 0) { print_err("pipe", e); return; }
    long pid = syscall3(SYS_fork, 0, 0, 0);
    if (pid < 0) { print_err("fork", pid); return; }
    if (pid == 0) {
        syscall3(SYS_setpgid, 0, 0, 0);  // 自分のプロセスグループを作る。Ctrl+C の SIGINT はこのグループに届く
        syscall3(SYS_close, sync_pipe[1], 0, 0);
        char c;
        syscall3(SYS_read, sync_pipe[0], &c, 1);  // 親が閉じると 0 が返る
        syscall3(SYS_close, sync_pipe[0], 0, 0);
        signal(SIGTTOU, SIG_DFL);  // 無視の設定は exec を越えて引き継がれるので戻す
        e = syscall3(SYS_execve, path, argv, child_env);
        print_err(path, e);
        syscall3(SYS_exit_group, 127, 0, 0);
    }
    syscall3(SYS_setpgid, pid, pid, 0);  // 子と親のどちらが先に走っても同じ状態にする
    syscall3(SYS_ioctl, 0, TIOCSPGRP, &pid);
    syscall3(SYS_close, sync_pipe[0], 0, 0);
    syscall3(SYS_close, sync_pipe[1], 0, 0);
    int status = 0;
    long r = syscall6(SYS_wait4, pid, (long)&status, 0, 0, 0, 0);
    int self = 1;
    syscall3(SYS_ioctl, 0, TIOCSPGRP, &self);  // 前面を init に戻す
    if (r < 0) { print_err("wait4", r); return; }
    if ((status & 0x7f) == 0) {
        int code = (status >> 8) & 0xff;
        if (code) { print("[exit "); print_u(code); print("]\n"); }
    } else {
        print("[signal "); print_u(status & 0x7f); print("]\n");
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
    spawn(cmd, line);
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

// init を新しいセッションのリーダーにし、端末を制御端末にする。
// これで Ctrl+C が前面のプロセスグループへの SIGINT になる。
// /dev/console は制御端末になれないので、実体の /dev/ttyS0 を開き直して fd 0〜2 にする（/dev のマウント後に呼ぶ）。
// ジョブ制御は作らないので Ctrl+Z（SIGTSTP）は無効にする。止まった子を待ち続けてしまうため
static void setup_tty(void) {
    signal(SIGTTOU, SIG_IGN);
    syscall3(SYS_setsid, 0, 0, 0);
    long fd = syscall3(SYS_open, "/dev/ttyS0", O_RDWR, 0);
    if (fd < 0) { status("Terminal /dev/ttyS0 (Ctrl+C disabled)", fd); return; }
    for (int i = 0; i < 3; i++) syscall3(SYS_dup2, fd, i, 0);
    if (fd > 2) syscall3(SYS_close, fd, 0, 0);
    long e = syscall3(SYS_ioctl, 0, TIOCSCTTY, 0);
    if (e < 0) status("Terminal /dev/ttyS0 (Ctrl+C disabled)", e);
    struct termios t;
    if (syscall3(SYS_ioctl, 0, TCGETS, &t) >= 0) {
        t.c_cc[VSUSP] = 0;
        syscall3(SYS_ioctl, 0, TCSETS, &t);
    }
}

void _start(void) {
    print("\033[2J\033[H");  // 画面クリア + カーソルを左上へ
    banner();
    mount_all();
    setup_tty();
    net_up();
    print("\n  Type 'help' for commands.\n\n");

    // 行編集（エコー、バックスペース）はカーネルの端末（canonical モード）に任せる
    char line[256];
    for (;;) {
        print("minios# ");
        long n = syscall3(SYS_read, 0, line, sizeof line - 1);
        if (n < 0) {
            // 端末が読めない（前面を取られたままなど）。プロンプトを連打しないよう 1 秒待つ
            print_err("read", n);
            struct { long sec, nsec; } t = {1, 0};
            syscall3(SYS_nanosleep, &t, 0, 0);
            continue;
        }
        if (n == 0) { print("\n"); continue; }  // Ctrl+D
        line[n] = 0;
        if (line[n - 1] == '\n') line[n - 1] = 0;
        run(line);
    }
}
