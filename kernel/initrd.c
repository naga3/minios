// initramfs（cpio の newc 形式）をそのまま読む。Linux 版と同じ build/initramfs.cpio を QEMU の -initrd で受け取る
#include "kernel.h"

static const u8 *base;
static u64 total;

void initrd_init(const void *data, u64 size) {
    base = data;
    total = size;
}

static u64 hex8(const u8 *p) {
    u64 v = 0;
    for (int i = 0; i < 8; i++) {
        u8 c = p[i];
        v = v * 16 + (c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return v;
}

static int name_eq(const char *name, const char *path) {
    while (*path == '/') path++;  // cpio の名前は先頭の / が無いことがある
    while (*name == '/') name++;
    while (*name && *name == *path) name++, path++;
    return *name == *path;
}

// path のファイルの中身と大きさ、モード（種類と権限）を返す。無ければ 0
const void *initrd_find(const char *path, u64 *size, u32 *mode) {
    u64 off = 0;
    while (off + 110 <= total) {
        const u8 *h = base + off;
        if (h[0] != '0' || h[5] != '1') return 0;  // "070701" のはず
        u64 filesize = hex8(h + 54), namesize = hex8(h + 94);
        const char *name = (const char *)h + 110;
        u64 data = (off + 110 + namesize + 3) & ~3UL;
        if (name_eq(name, "TRAILER!!!")) return 0;
        if (name_eq(name, path)) {
            *size = filesize;
            *mode = hex8(h + 14);
            return base + data;
        }
        off = (data + filesize + 3) & ~3UL;
    }
    return 0;
}
