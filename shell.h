// シェル: コマンドの解釈と実行（組み込みコマンド、リダイレクト、外部プログラム）と、端末ごとのセッション。
// init（PID 1）が端末ごとに fork して session を呼ぶ
#pragma once
#include "boot.h"
#include "sha256.h"
#include "seccomp.h"

// ---- コマンド ----

static void cmd_help(char *args);
static void cmd_exit(char *args);
static void cmd_trust(char *args);
static void run(char *line);

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

// 1970-01-01 からの日数を年月日に（Howard Hinnant の civil_from_days）
static void civil_from_days(long z, long *y, unsigned *m, unsigned *d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = z - era * 146097;
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = yoe + era * 400 + (*m <= 2);
}

static void print_2(unsigned v) {
    if (v < 10) print("0");
    print_u(v);
}

// 今の時刻を "2026-09-28T01:02:03Z" の形で out に書く
static void utc_now(char out[21]) {
    struct { long sec, nsec; } t;
    syscall3(SYS_clock_gettime, 0 /* CLOCK_REALTIME */, &t, 0);
    long y;
    unsigned m, d;
    civil_from_days(t.sec / 86400, &y, &m, &d);
    long s = t.sec % 86400;
    unsigned v[6] = {y, m, d, s / 3600, s / 60 % 60, s % 60};
    char *p = out;
    for (int i = 0; i < 6; i++) {
        if (i == 0) {
            for (int k = 3; k >= 0; k--) p[k] = '0' + v[0] % 10, v[0] /= 10;
            p += 4;
        } else {
            *p++ = "--T::"[i - 1];
            *p++ = '0' + v[i] / 10;
            *p++ = '0' + v[i] % 10;
        }
    }
    *p++ = 'Z';
    *p = 0;
}

static void cmd_date(char *args) {
    (void)args;
    char now[21];
    utc_now(now);
    now[10] = ' ';  // "2026-09-28 01:02:03 UTC" の形で出す
    now[19] = 0;
    print(now); print(" UTC\n");
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

static void cat_fd(long fd, const char *name) {
    char buf[4096];
    long n;
    while ((n = syscall3(SYS_read, fd, buf, sizeof buf)) > 0) write_all(1, buf, n);
    if (n < 0) print_err(name, n);
}

// cat [file...]: ファイルを出す。引数が無ければ標準入力を出す（パイプの途中で使う）
static void cmd_cat(char *args) {
    char *path = next_word(&args);
    if (!path) { cat_fd(0, "stdin"); return; }
    do {
        long fd = syscall3(SYS_open, path, O_RDONLY, 0);
        if (fd < 0) { print_err(path, fd); continue; }
        cat_fd(fd, path);
        syscall3(SYS_close, fd, 0, 0);
    } while ((path = next_word(&args)));
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

// source <file>: ファイルの各行をコマンドとして実行する。# 以降はコメント
static void source_file(const char *path, int quiet_if_missing) {
    static int depth;
    if (depth >= 8) { print_err(path, -40 /* ELOOP */); return; }
    static char bufs[8][4096];
    char *buf = bufs[depth];
    long n = read_file(path, buf, sizeof bufs[0]);
    if (n < 0) {
        if (!quiet_if_missing || n != -2) print_err(path, n);
        return;
    }
    depth++;
    char *line = buf;
    while (*line) {
        char *end = line;
        while (*end && *end != '\n') end++;
        char *next = *end ? end + 1 : end;
        *end = 0;
        for (char *p = line; *p; p++)
            if (*p == '#') { *p = 0; break; }
        run(line);
        line = next;
    }
    depth--;
}

static void cmd_source(char *args) {
    char *path = next_word(&args);
    if (!path) { print_err("usage: source <file>", -22); return; }
    source_file(path, 0);
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
    // write は端末の送信バッファに入れた時点で戻る。すぐ電源を切ると送り出される前に消えるので、送信が終わるのを待つ
    syscall3(SYS_ioctl, 1, TCSBRK, 1);
    power_off();
}

static const struct {
    const char *name;
    void (*fn)(char *args);
    const char *help;
} commands[] = {
    {"help",     cmd_help,     "show this help"},
    {"uname",    cmd_uname,    "kernel name and version"},
    {"uptime",   cmd_uptime,   "seconds since boot"},
    {"date",     cmd_date,     "current date and time (UTC)"},
    {"mem",      cmd_mem,      "memory usage"},
    {"ps",       cmd_ps,       "list processes"},
    {"dmesg",    cmd_dmesg,    "kernel log"},
    {"mounts",   cmd_mounts,   "mounted filesystems"},
    {"ls",       cmd_ls,       "list directory"},
    {"cat",      cmd_cat,      "print files (stdin if none)"},
    {"mkdir",    cmd_mkdir,    "make directories"},
    {"rm",       cmd_rm,       "remove files or empty directories"},
    {"trust",    cmd_trust,    "trust <name>: allow a program you put in /data/bin"},
    {"chmod",    cmd_chmod,    "chmod <octal-mode> <path>..."},
    {"cd",       cmd_cd,       "change directory"},
    {"pwd",      cmd_pwd,      "print working directory"},
    {"df",       cmd_df,       "disk usage"},
    {"sync",     cmd_sync,     "flush writes to disk"},
    {"ifconfig", cmd_ifconfig, "network interfaces"},
    {"ping",     cmd_ping,     "ping <a.b.c.d> [count]"},
    {"source",   cmd_source,   "run commands from a file"},
    {"echo",     cmd_echo,     "print arguments (cmd > file, >> to append, < file for input)"},
    {"clear",    cmd_clear,    "clear the screen"},
    {"exit",     cmd_exit,     "end this shell (a new one starts)"},
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

// 組み込みコマンドの名前を /etc/builtins に 1 行ずつ書く（init が起動時に呼ぶ）。
// 組み込みは /bin などにファイルが無いので、grow などのプログラムが「その名前は使われている」と知る手段がこれしかない
static void write_builtins_file(void) {
    long fd = syscall3(SYS_open, "/etc/builtins", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    for (size_t i = 0; i < NCOMMANDS; i++) {
        write_all(fd, commands[i].name, slen(commands[i].name));
        write_all(fd, "\n", 1);
    }
    syscall3(SYS_close, fd, 0, 0);
}

// ---- 信頼（/data/bin のプログラムは、記録があり、ハッシュが一致するものだけ実行する） ----

#define TRUST_DIR    "/data/bin"
#define RECORD_DIR   "/data/bin/.grow"

// 記録の中の "key 値" の行を探し、値の先頭を返す（無ければ 0）。値は改行か NUL で終わる
static const char *record_value(const char *rec, const char *key) {
    for (const char *p = rec; *p;) {
        size_t i = 0;
        while (key[i] && p[i] == key[i]) i++;
        if (!key[i] && p[i] == ' ') return p + i + 1;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return 0;
}

// path が /data/bin のプログラムなら、記録（/data/bin/.grow/<名前>）の binary-sha256 と照らし合わせる。
// 実行してよければ 1。だめなら理由を stderr に出して 0。/bin（OS のイメージ）や明示的な他のパスは調べない。
// 記録に syscalls（許すシステムコールの一覧）があれば、syscalls に写す（無ければ空。人が trust したものは制限しない）
static int trusted(const char *path, char *syscalls, size_t size) {
    if (syscalls) syscalls[0] = 0;
    const char *prefix = TRUST_DIR "/";
    size_t n = slen(prefix);
    for (size_t i = 0; i < n; i++)
        if (path[i] != prefix[i]) return 1;
    const char *name = path + n;
    char rec_path[256], rec[4096], hex[65];
    size_t k = 0;
    str_append(rec_path, sizeof rec_path, &k, RECORD_DIR "/");
    str_append(rec_path, sizeof rec_path, &k, name);
    const char *why = 0;
    if (read_file(rec_path, rec, sizeof rec) < 0) {
        why = ": not trusted (no record; install it with grow, or mark it with trust)\n";
    } else if (sha256_file(path, hex) < 0) {
        why = ": cannot read\n";
    } else {
        const char *want = record_value(rec, "binary-sha256");
        int same = want != 0;
        for (int i = 0; same && i < 64; i++) same = want[i] == hex[i];
        if (same && want[64] != '\n' && want[64] != 0) same = 0;
        if (!same) why = ": not trusted (changed since it was installed; the hash does not match the record)\n";
    }
    if (!why) {
        const char *sc = record_value(rec, "syscalls");
        if (sc && syscalls) {
            size_t k = 0;
            while (sc[k] && sc[k] != '\n' && k < size - 1) { syscalls[k] = sc[k]; k++; }
            syscalls[k] = 0;
        }
        return 1;
    }
    write_all(2, name, slen(name));
    write_all(2, why, slen(why));
    return 0;
}

// trust <名前>: 人が自分の責任で /data/bin に置いたプログラムに、記録（今のハッシュ）を付ける
static void cmd_trust(char *args) {
    char *name = next_word(&args);
    if (!name) { print_err("usage: trust <name in /data/bin>", -22); return; }
    for (char *p = name; *p; p++)
        if (*p == '/') { print_err("trust: give a name in /data/bin, not a path", -22); return; }
    char path[256], rec_path[256], hex[65], now[21];
    size_t n = 0, k = 0;
    str_append(path, sizeof path, &n, TRUST_DIR "/");
    str_append(path, sizeof path, &n, name);
    str_append(rec_path, sizeof rec_path, &k, RECORD_DIR "/");
    str_append(rec_path, sizeof rec_path, &k, name);
    long e = sha256_file(path, hex);
    if (e < 0) { print_err(path, e); return; }
    syscall3(SYS_mkdir, RECORD_DIR, 0755, 0);
    long fd = syscall3(SYS_open, rec_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { print_err(rec_path, fd); return; }
    utc_now(now);
    const char *parts[] = {"name ", name, "\ntrusted-by human\nbinary-sha256 ", hex, "\ninstalled ", now, "\n"};
    for (size_t i = 0; i < sizeof parts / sizeof parts[0]; i++) write_all(fd, parts[i], slen(parts[i]));
    syscall3(SYS_close, fd, 0, 0);
    syscall3(SYS_sync, 0, 0, 0);
    print("trusted "); print(path); print(" sha256 "); print(hex); print("\n");
}

static int find_builtin(const char *name) {
    for (size_t i = 0; i < NCOMMANDS; i++)
        if (seq(name, commands[i].name)) return i;
    return -1;
}

// リダイレクト（"< file"、"> file"、">> file"）を行から取り除き（空白に置き換える）、開いたファイルを fds に返す。
// fds[0] は標準入力、fds[1] は標準出力の行き先（無ければ -1）。0 か負のエラー番号（エラーは出力済み）
static long open_redirects(char *line, long fds[2]) {
    fds[0] = fds[1] = -1;
    for (char *p = line; *p; p++) {
        if (*p != '<' && *p != '>') continue;
        int in = *p == '<';
        int flags = in ? O_RDONLY : O_WRONLY | O_CREAT | O_TRUNC;
        *p = ' ';
        char *w = p + 1;
        if (!in && *w == '>') {
            flags = O_WRONLY | O_CREAT | O_APPEND;
            *w++ = ' ';
        }
        while (*w == ' ' || *w == '\t') w++;
        char *e = w;
        while (*e && *e != ' ' && *e != '\t' && *e != '<' && *e != '>') e++;
        long fd = -22;
        if (e == w) {
            print_err(in ? "<" : ">", fd);
        } else {
            char c = *e;
            *e = 0;
            fd = syscall3(SYS_open, w, flags, 0644);
            if (fd < 0) print_err(w, fd);
            *e = c;
        }
        if (fd < 0) {
            for (int i = 0; i < 2; i++)
                if (fds[i] >= 0) syscall3(SYS_close, fds[i], 0, 0);
            return fd;
        }
        for (char *k = w; k < e; k++) *k = ' ';
        if (fds[!in] >= 0) syscall3(SYS_close, fds[!in], 0, 0);
        fds[!in] = fd;
        p = e - 1;
    }
    return 0;
}

// 子プロセスの中で 1 段を実行する。組み込みコマンドならその場で実行して終わり、外部プログラムなら execve する
static void __attribute__((noreturn)) exec_stage(char *line) {
    // シェルが無視しているシグナルは exec を越えて引き継がれるので戻す
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    long fds[2];
    if (open_redirects(line, fds) < 0) syscall3(SYS_exit_group, 1, 0, 0);
    for (int i = 0; i < 2; i++) {
        if (fds[i] < 0) continue;
        syscall3(SYS_dup2, fds[i], i, 0);
        syscall3(SYS_close, fds[i], 0, 0);
    }
    char *cmd = next_word(&line);
    if (!cmd) syscall3(SYS_exit_group, 0, 0, 0);
    int b = find_builtin(cmd);
    if (b >= 0) {
        commands[b].fn(line);
        syscall3(SYS_exit_group, 0, 0, 0);
    }
    char path[256];
    if (!find_program(cmd, path, sizeof path)) {
        write_all(2, cmd, slen(cmd));
        write_lit(2, ": command not found\n");
        syscall3(SYS_exit_group, 127, 0, 0);
    }
    // 記録に許すシステムコールの一覧があれば、その分だけを許して（seccomp）から exec する
    char syscalls[512];
    if (!trusted(path, syscalls, sizeof syscalls)) syscall3(SYS_exit_group, 126, 0, 0);
    if (syscalls[0]) {
        const char *bad;
        size_t bad_len;
        long e = apply_syscall_filter(syscalls, &bad, &bad_len);
        if (e < 0) {
            const char *msg = ": cannot apply the system call restriction in its record\n";
            write_all(2, cmd, slen(cmd));
            write_all(2, msg, slen(msg));
            syscall3(SYS_exit_group, 126, 0, 0);
        }
    }
    char *argv[256];  // 31 までだと、grow への長い依頼（英語で 31 語を超える）が途中で切れた（周 30）
    int argc = 0;
    argv[argc++] = cmd;
    char *w;
    while (argc < 255 && (w = next_word(&line))) argv[argc++] = w;
    argv[argc] = 0;
    long e = syscall3(SYS_execve, path, argv, child_env);
    print_err(path, e);
    syscall3(SYS_exit_group, 127, 0, 0);
    for (;;) {}
}

#define MAX_STAGES 8

// 各段を子プロセスで動かし、パイプでつなぐ（a | b | c）。外部プログラム 1 つだけの実行も 1 段のパイプラインとして扱う。
// 全段を 1 つのプロセスグループにして端末の前面に立たせるので、Ctrl+C の SIGINT は全段に届く。
// 前面の切り替えはシェルの仕事（子が自分で TIOCSPGRP すると、孤児のプロセスグループ扱いで EIO になることがある）。
// 切り替えが済むまで子を sync パイプで待たせる。前面を取り戻すときシェルは背景にいるので、SIGTTOU を無視しておく（session）
static void pipeline(char **stages, int n) {
    int sync_pipe[2];
    long e = syscall3(SYS_pipe, sync_pipe, 0, 0);
    if (e < 0) { print_err("pipe", e); return; }
    long pids[MAX_STAGES], pgid = 0;
    int started = 0, prev_read = -1;
    for (int i = 0; i < n; i++) {
        int p[2] = {-1, -1};
        if (i < n - 1 && (e = syscall3(SYS_pipe, p, 0, 0)) < 0) { print_err("pipe", e); break; }
        long pid = syscall3(SYS_fork, 0, 0, 0);
        if (pid < 0) { print_err("fork", pid); break; }
        if (pid == 0) {
            syscall3(SYS_setpgid, 0, pgid, 0);  // 1 段目は自分のグループを作り、2 段目以降はそこに入る
            syscall3(SYS_close, sync_pipe[1], 0, 0);
            char c;
            syscall3(SYS_read, sync_pipe[0], &c, 1);  // シェルが閉じると 0 が返る
            syscall3(SYS_close, sync_pipe[0], 0, 0);
            if (prev_read >= 0) {
                syscall3(SYS_dup2, prev_read, 0, 0);
                syscall3(SYS_close, prev_read, 0, 0);
            }
            if (p[1] >= 0) {
                syscall3(SYS_dup2, p[1], 1, 0);
                syscall3(SYS_close, p[0], 0, 0);
                syscall3(SYS_close, p[1], 0, 0);
            }
            exec_stage(stages[i]);
        }
        if (!pgid) pgid = pid;
        syscall3(SYS_setpgid, pid, pgid, 0);  // 子とシェルのどちらが先に走っても同じ状態にする
        pids[started++] = pid;
        // シェルは使わない端を閉じる。閉じ忘れると、読む側に EOF が届かず止まらない
        if (prev_read >= 0) syscall3(SYS_close, prev_read, 0, 0);
        if (p[1] >= 0) syscall3(SYS_close, p[1], 0, 0);
        prev_read = p[0];
    }
    if (prev_read >= 0) syscall3(SYS_close, prev_read, 0, 0);
    if (pgid) syscall3(SYS_ioctl, 0, TIOCSPGRP, &pgid);
    syscall3(SYS_close, sync_pipe[0], 0, 0);
    syscall3(SYS_close, sync_pipe[1], 0, 0);

    // 全段を待つ。報告するのは最後の段の終わり方（シェルの慣習）
    int status = 0;
    for (int i = 0; i < started; i++) {
        int st = 0;
        if (syscall6(SYS_wait4, pids[i], (long)&st, 0, 0, 0, 0) >= 0 && i == started - 1) status = st;
    }
    int self = syscall3(SYS_getpid, 0, 0, 0);
    syscall3(SYS_ioctl, 0, TIOCSPGRP, &self);  // 前面をシェルに戻す
    if (!started) return;
    if ((status & 0x7f) == 0) {
        int code = (status >> 8) & 0xff;
        if (code) { print("[exit "); print_u(code); print("]\n"); }
    } else {
        print("[signal "); print_u(status & 0x7f);
        if ((status & 0x7f) == SIGSYS) print(": used a system call that is not allowed");
        print("]\n");
    }
}

// 1 行を実行する。パイプを含めばパイプライン。含まなければ、組み込みコマンドはシェル自身の中で
// （cd や exit が効くように）、外部プログラムは 1 段のパイプラインとして子プロセスで実行する
static void run(char *line) {
    char *stages[MAX_STAGES];
    int n = 0;
    stages[n++] = line;
    for (char *p = line; *p; p++) {
        if (*p != '|') continue;
        if (n == MAX_STAGES) { print_err("|", -7 /* E2BIG */); return; }
        *p = 0;
        stages[n++] = p + 1;
    }
    for (int i = 0; i < n && n > 1; i++) {
        const char *t = stages[i];
        while (*t == ' ' || *t == '\t') t++;
        if (!*t) { print_err("|", -22); return; }  // 空の段（"a | | b"）
    }
    if (n > 1) { pipeline(stages, n); return; }

    // 先頭の単語が組み込みコマンドか（next_word は行を書き換えるので、写しで調べる）
    char first[32];
    size_t k = 0;
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    while (*p && *p != ' ' && *p != '\t' && *p != '>' && k < sizeof first - 1) first[k++] = *p++;
    first[k] = 0;
    if (!k) return;
    int b = find_builtin(first);
    if (b < 0) {
        // 見つからなければ fork する前に知らせる（子の中で知らせると [exit 127] も出て煩い）
        char path[256];
        if (!find_program(first, path, sizeof path)) {
            write_all(2, first, slen(first));
            write_lit(2, ": command not found\n");
            return;
        }
        if (!trusted(path, 0, 0)) return;  // fork する前に知らせる
        pipeline(stages, 1);
        return;
    }

    // 組み込みコマンドはシェルの中で動くので、差し替えた標準入出力を後で戻す
    long fds[2], saved[2] = {-1, -1};
    if (open_redirects(line, fds) < 0) return;
    for (int i = 0; i < 2; i++) {
        if (fds[i] < 0) continue;
        saved[i] = syscall3(SYS_dup, i, 0, 0);
        syscall3(SYS_dup2, fds[i], i, 0);
        syscall3(SYS_close, fds[i], 0, 0);
    }
    char *rest = line;
    next_word(&rest);
    commands[b].fn(rest);
    for (int i = 0; i < 2; i++) {
        if (saved[i] < 0) continue;
        syscall3(SYS_dup2, saved[i], i, 0);
        syscall3(SYS_close, saved[i], 0, 0);
    }
}

// ---- シェル（端末ごとのセッション） ----

#define SYS_prctl   157
#define PR_SET_NAME 15

static void cmd_exit(char *args) {
    (void)args;
    syscall3(SYS_exit_group, 0, 0, 0);
}

// 端末 tty で対話するシェル。init が端末ごとに fork して起動し、終わったら起動し直す。
// 新しいセッションのリーダーになり、端末を制御端末にする。これで Ctrl+C が前面のプロセスグループへの SIGINT になる。
// シェル自身は Ctrl+C（SIGINT）と Ctrl+\（SIGQUIT）を無視する。プロンプトで押したときに前面にいるのはシェルなので。
// ジョブ制御は作らないので Ctrl+Z（SIGTSTP）は無効にする。止まった子を待ち続けてしまうため
static void __attribute__((noreturn)) session(const char *tty, int first) {
    syscall3(SYS_prctl, PR_SET_NAME, "sh", 0);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    syscall3(SYS_setsid, 0, 0, 0);
    long fd = syscall3(SYS_open, tty, O_RDWR, 0);
    if (fd < 0) syscall3(SYS_exit_group, 1, 0, 0);
    for (int i = 0; i < 3; i++) syscall3(SYS_dup2, fd, i, 0);
    if (fd > 2) syscall3(SYS_close, fd, 0, 0);
    syscall3(SYS_ioctl, 0, TIOCSCTTY, 0);
    struct termios t;
    if (syscall3(SYS_ioctl, 0, TCGETS, &t) >= 0) {
        t.c_cc[VSUSP] = 0;
        syscall3(SYS_ioctl, 0, TCSETS, &t);
    }
    if (first) {
        // シリアルには init が起動ログを出している。画面の端末では改めてクリアしてバナーを出す
        if (!seq(tty, "/dev/ttyS0")) {
            print("\033[2J\033[H");
            banner();
        }
        print("\n  Type 'help' for commands.\n\n");
    }

    // 行編集（エコー、バックスペース）はカーネルの端末（canonical モード）に任せる
    // 端末（canonical モード）は 1 行を最大 4096 バイトまで溜める。それより短い入れ物だと、
    // 読み残した後半が次の読み込み（grow の承認の [y/N] など）に混ざる（周 30 で踏んだ）
    static char line[4097];
    for (;;) {
        print("minios# ");
        long n = syscall3(SYS_read, 0, line, sizeof line - 1);
        if (n < 0) {
            // 端末が読めない（前面を取られたままなど）。プロンプトを連打しないよう 1 秒待つ
            print_err("read", n);
            struct { long sec, nsec; } ts = {1, 0};
            syscall3(SYS_nanosleep, &ts, 0, 0);
            continue;
        }
        if (n == 0) { print("\n"); syscall3(SYS_exit_group, 0, 0, 0); }  // Ctrl+D で終わる。init が起動し直す
        line[n] = 0;
        if (line[n - 1] != '\n') {
            // 改行まで読めなかった（長すぎる行）。残りを読み捨てて、実行しない
            char rest[256];
            long r;
            do r = syscall3(SYS_read, 0, rest, sizeof rest); while (r > 0 && rest[r - 1] != '\n');
            print_err("line too long", -7 /* E2BIG */);
            continue;
        }
        line[n - 1] = 0;
        run(line);
    }
}

