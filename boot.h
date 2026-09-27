// 起動処理: バナー、マウント（/proc・/sys・/dev・/data）、起動メディアの検出、ネットワーク（DHCP）、終了時のアンマウント。
// init（PID 1）が起動時に一度だけ呼ぶ。シェルの shutdown も unmount_all を使う
#pragma once
#include "lib.h"
#include "net.h"

#define VERSION "0.11"

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

#define MS_RDONLY 1
#define MS_BIND   4096

struct block_list { char names[16][32]; int n; };

static void collect_block(const char *name, unsigned char type, void *ctx) {
    (void)type;
    struct block_list *l = ctx;
    if (name[0] == '.' || l->n >= 16) return;
    size_t i = 0;
    for (; name[i] && i < sizeof l->names[0] - 1; i++) l->names[l->n][i] = name[i];
    l->names[l->n][i] = 0;
    l->n++;
}

// UEFI で起動したときの起動メディア（EFI/BOOT/BOOTX64.EFI がある FAT）を探して /boot にマウントする。
// USB メモリの認識には数秒かかるので、最大 10 秒待つ。見つけたデバイス名を dev に入れて 0 を返す
static long mount_boot_media(char *dev, size_t size) {
    for (int tries = 0; tries < 50; tries++) {
        struct block_list l = {0};
        each_dirent("/sys/class/block", collect_block, &l);
        for (int i = 0; i < l.n; i++) {
            join_path(dev, size, "/dev", l.names[i]);
            // FAT には実行権限が無いので全ファイルを 755 扱いにし、chmod はエラーにしない
            if (syscall6(SYS_mount, (long)dev, (long)"/boot", (long)"vfat", 0, (long)"fmask=0022,dmask=0022,quiet", 0) < 0)
                continue;
            struct stat st;
            if (syscall3(SYS_stat, "/boot/EFI/BOOT/BOOTX64.EFI", &st, 0) >= 0) return 0;
            syscall3(SYS_umount2, "/boot", 0, 0);
        }
        struct { long sec, nsec; } ts = {0, 200000000};
        syscall3(SYS_nanosleep, &ts, 0, 0);
    }
    return -2;
}

// 起動時のマウント。/data は永続ディスク（virtio の minix か、UEFI で起動した USB メモリの FAT）。無ければメモリだけで動く
static void mount_all(void) {
    status("Mounted /proc", mount("proc", "/proc", "proc"));
    status("Mounted /sys", mount("sysfs", "/sys", "sysfs"));
    status("Mounted /dev", mount("devtmpfs", "/dev", "devtmpfs"));
    // virtio ディスクを順に調べる。minix なら永続ディスク（/data）、ext4 ならモデル用のディスク（/models、読み取り専用）
    struct stat st;
    int data = 0;
    for (char c = 'a'; c <= 'd'; c++) {
        char dev[] = "/dev/vdX", what[64];
        size_t n = 0;
        dev[7] = c;
        if (syscall3(SYS_stat, dev, &st, 0) < 0) break;
        if (!data && mount(dev, "/data", "minix") >= 0) {
            data = 1;
            str_append(what, sizeof what, &n, "Mounted /data (");
            str_append(what, sizeof what, &n, dev);
            str_append(what, sizeof what, &n, ")");
            status(what, 0);
            continue;
        }
        long e = syscall6(SYS_mount, (long)dev, (long)"/models", (long)"ext4", MS_RDONLY, 0, 0);
        str_append(what, sizeof what, &n, e >= 0 ? "Mounted /models (" : "Unknown disk (");
        str_append(what, sizeof what, &n, dev);
        str_append(what, sizeof what, &n, e >= 0 ? ", read-only)" : ")");
        status(what, e);
    }
    if (data) return;
    // UEFI で起動していれば、起動した USB メモリなどの FAT に minios-data を作って /data にする
    if (syscall3(SYS_stat, "/sys/firmware/efi", &st, 0) >= 0) {
        char dev[64];
        long e = mount_boot_media(dev, sizeof dev);
        if (e >= 0) {
            syscall3(SYS_mkdir, "/boot/minios-data", 0755, 0);
            e = syscall6(SYS_mount, (long)"/boot/minios-data", (long)"/data", 0, MS_BIND, 0, 0);
            char what[96] = "Mounted /data (";
            size_t n = slen(what);
            for (const char *p = dev; *p && n < sizeof what - 20; p++) what[n++] = *p;
            for (const char *p = " minios-data)"; *p; p++) what[n++] = *p;
            what[n] = 0;
            status(what, e);
            return;
        }
    }
    print("  [ -- ] No disk: /data is not persistent\n");
}

static unsigned int ip(const char *s) {
    int ok;
    return parse_ip(s, &ok);
}

// lo 以外で最初に見つかったネットワークインターフェース
static void pick_iface(const char *name, unsigned char type, void *ctx) {
    (void)type;
    char *out = ctx;
    if (out[0] || name[0] == '.' || seq(name, "lo")) return;
    for (int i = 0; name[i] && i < 15; i++) out[i] = name[i];
}

static void write_resolv_conf(unsigned int dns) {
    long fd = syscall3(SYS_open, "/etc/resolv.conf", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { status("DNS (/etc/resolv.conf)", fd); return; }
    int saved = syscall3(SYS_dup, 1, 0, 0);
    syscall3(SYS_dup2, fd, 1, 0);
    print("nameserver "); print_ip(dns); print("\n");
    syscall3(SYS_dup2, saved, 1, 0);
    syscall3(SYS_close, saved, 0, 0);
    syscall3(SYS_close, fd, 0, 0);
    print("  [ \033[32mOK\033[0m ] DNS "); print_ip(dns); print(" (/etc/resolv.conf)\n");
}

// lo を上げ、最初の NIC を DHCP で設定する（アドレス、マスク、デフォルトルート、DNS）
static void net_up(void) {
    long sock = syscall3(SYS_socket, AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { status("Network", sock); return; }
    long e = if_set_addr(sock, "lo", ip("127.0.0.1"), ip("255.0.0.0"));
    if (e >= 0) e = if_up(sock, "lo");
    status("Loopback lo 127.0.0.1", e);

    char ifname[16] = {0};
    each_dirent("/sys/class/net", pick_iface, ifname);
    if (!ifname[0]) {
        print("  [ -- ] No network interface\n");
        syscall3(SYS_close, sock, 0, 0);
        return;
    }
    struct dhcp_lease lease;
    e = if_up(sock, ifname);
    if (e >= 0) e = dhcp(sock, ifname, &lease);
    if (e >= 0) e = if_set_addr(sock, ifname, lease.addr, lease.mask ? lease.mask : ip("255.255.255.0"));
    if (e >= 0 && lease.router) e = route_add_default(sock, lease.router);
    if (e < 0) {
        char what[48] = "Network ";
        size_t n = slen(what);
        for (const char *p = ifname; *p; p++) what[n++] = *p;
        for (const char *p = " (DHCP)"; *p; p++) what[n++] = *p;
        what[n] = 0;
        status(what, e);
    } else {
        print("  [ \033[32mOK\033[0m ] Network "); print(ifname); print(" "); print_ip(lease.addr);
        print(" via "); print_ip(lease.router); print(" (DHCP)\n");
        // ネットワークを設定した init が DNS の設定も書く。無いと Go などは [::1]:53 に問い合わせてしまう
        if (lease.dns) write_resolv_conf(lease.dns);
    }
    syscall3(SYS_close, sock, 0, 0);
}

// 書き込み待ちのデータをディスクへ書き出し、/data と /boot を外す
static void unmount_all(void) {
    syscall3(SYS_sync, 0, 0, 0);
    syscall3(SYS_umount2, "/data", 0, 0);
    syscall3(SYS_umount2, "/boot", 0, 0);
}

