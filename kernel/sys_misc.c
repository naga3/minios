// その他のシステムコール: mount（devtmpfs だけ）、uname、sysinfo、clock_gettime、reboot、statfs、sync
#include "kernel.h"

// ---- 時計: 起動時に CMOS の時計（RTC）を読み、あとはタイマーの刻みを足す ----

static u64 boot_epoch;  // 起動したときの時刻（1970 年からの秒）

static u8 cmos(u8 reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static u64 days_from_civil(long y, unsigned m, unsigned d) {  // Howard Hinnant の days_from_civil
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = y - era * 400;
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void clock_init(void) {
    while (cmos(0x0A) & 0x80) {}  // 更新中でないときに読む
    u8 s = cmos(0), mi = cmos(2), h = cmos(4), d = cmos(7), mo = cmos(8), y = cmos(9), b = cmos(0x0B);
    if (!(b & 4)) {  // BCD なら 2 進に
#define BCD(x) (((x) & 15) + ((x) >> 4) * 10)
        s = BCD(s), mi = BCD(mi), h = BCD(h & 0x7f) | (h & 0x80), d = BCD(d), mo = BCD(mo), y = BCD(y);
    }
    boot_epoch = days_from_civil(2000 + y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}

long sys_clock_gettime(long clock, u64 ts_va) {
    long ts[2] = {ticks / 100, ticks % 100 * 10000000};
    if (clock == 0) ts[0] += boot_epoch;  // CLOCK_REALTIME
    return copy_out(ts_va, ts, sizeof ts);
}

// ---- uname ----

long sys_uname(u64 va) {
    char u[6][65];
    kmemset(u, 0, sizeof u);
    const char *f[] = {"minios", "minios", "0.1-scratch", "#1", "x86_64", "(none)"};
    for (int i = 0; i < 6; i++)
        for (int j = 0; f[i][j]; j++) u[i][j] = f[i][j];
    return copy_out(va, u, sizeof u);
}

// ---- sysinfo ----

long sys_sysinfo(u64 va) {
    struct {
        long uptime;
        u64 loads[3], totalram, freeram, sharedram, bufferram, totalswap, freeswap;
        u16 procs, pad;
        u64 totalhigh, freehigh;
        u32 mem_unit;
    } si;
    kmemset(&si, 0, sizeof si);
    si.uptime = ticks / 100;
    si.totalram = mm_total_pages() * PAGE;
    si.freeram = mm_free_pages() * PAGE;
    si.mem_unit = 1;
    for (int i = 0; i < NPROC; i++)
        if (procs[i].state != UNUSED) si.procs++;
    return copy_out(va, &si, sizeof si);
}

// ---- mount: devtmpfs だけ（/dev にデバイスを作る） ----

static void mknod(struct inode *dir, const char *name, u32 rdev) {
    u64 len = 0;
    while (name[len]) len++;
    if (dir_lookup(dir, name, len)) return;
    struct inode *ip = inode_new(I_CHR, 0666);
    ip->rdev = rdev;
    dir_add(dir, ip, name, len);
}

long sys_mount(u64 src_va, u64 dir_va, u64 type_va) {
    char type[32], dir[256];
    long e = copy_str_in(type, type_va, sizeof type);
    if (e < 0) return e;
    if ((e = copy_str_in(dir, dir_va, sizeof dir)) < 0) return e;
    (void)src_va;
    struct inode *d = vfs_lookup_at(cur->cwd, dir, 1, &e);
    if (!d) return e;
    if (d->type != I_DIR) return -20;
    const char *t = type;
    if (t[0] == 'd' && t[1] == 'e' && t[2] == 'v' && t[3] == 't') {  // devtmpfs
        mknod(d, "console", 5 << 8 | 1);
        mknod(d, "tty", 5 << 8 | 0);
        mknod(d, "ttyS0", 4 << 8 | 64);
        mknod(d, "null", 1 << 8 | 3);
        mknod(d, "zero", 1 << 8 | 5);
        return 0;
    }
    return -19;  // ENODEV（proc・sysfs・ディスクのファイルシステムはまだ無い）
}

// ---- reboot: 電源断も再起動も、QEMU を終わらせる ----

long sys_reboot(long magic1, long magic2, long cmd) {
    if ((u32)magic1 != 0xfee1dead || magic2 != 672274793) return -22;
    kprint("[reboot: "); kprint(cmd == 0x4321fedc ? "power off" : "restart"); kprint("]\n");
    qemu_exit(0);
}

// ---- statfs: メモリ上のファイルシステムの容量（空いている物理メモリ） ----

long sys_statfs(u64 path_va, u64 va) {
    char path[256];
    long e = copy_str_in(path, path_va, sizeof path);
    if (e < 0) return e;
    if (!vfs_lookup_at(cur->cwd, path, 1, &e)) return e;
    long f[15];
    kmemset(f, 0, sizeof f);
    f[0] = 0x858458f6;  // RAMFS_MAGIC
    f[1] = PAGE;        // f_bsize
    f[2] = 0;           // f_blocks（ramfs と同じく 0。df は出さない）
    f[8] = NAME_MAX;    // f_namelen
    return copy_out(va, f, sizeof f);
}
