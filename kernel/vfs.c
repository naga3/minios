// ファイルシステム: メモリ上の木（ramfs）。起動時に initramfs（cpio）の中身で作る。
// initramfs のファイルの中身は initramfs のメモリを指したまま使い、書き換えるときに初めてコピーする
#include "kernel.h"

struct inode *vfs_root;
static u64 next_ino = 1;

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static void copy_name(char *dst, const char *src, u64 n) {
    u64 i = 0;
    for (; i < n && i < NAME_MAX - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

struct inode *inode_new(int type, u32 mode) {
    struct inode *ip = kmalloc(sizeof *ip);
    if (!ip) return 0;
    ip->type = type;
    ip->mode = mode & 07777;
    ip->ino = next_ino++;
    return ip;
}

struct inode *dir_lookup(struct inode *dir, const char *name, u64 len) {
    if (len == 1 && name[0] == '.') return dir;
    if (len == 2 && name[0] == '.' && name[1] == '.') return dir->parent ? dir->parent : dir;
    for (struct inode *c = dir->child; c; c = c->next) {
        u64 i = 0;
        while (i < len && c->name[i] == name[i]) i++;
        if (i == len && !c->name[len]) return c;
    }
    return 0;
}

void dir_add(struct inode *dir, struct inode *ip, const char *name, u64 len) {
    copy_name(ip->name, name, len);
    ip->parent = dir;
    // 名前の順に並べておく（ls の出力を安定させる）
    struct inode **pp = &dir->child;
    while (*pp) {
        const char *a = (*pp)->name, *b = ip->name;
        while (*a && *a == *b) a++, b++;
        if ((u8)*a > (u8)*b) break;
        pp = &(*pp)->next;
    }
    ip->next = *pp;
    *pp = ip;
}

void dir_remove(struct inode *dir, struct inode *ip) {
    for (struct inode **pp = &dir->child; *pp; pp = &(*pp)->next)
        if (*pp == ip) {
            *pp = ip->next;
            ip->next = 0;
            return;
        }
}

// 中身を書き換えられるようにする（initramfs を指していればコピーし、足りなければ広げる）
int inode_reserve(struct inode *ip, u64 need) {
    if (!ip->borrowed && need <= ip->cap) return 0;
    u64 cap = ip->cap * 2;
    if (cap < need) cap = need;
    if (cap < 256) cap = 256;
    u8 *d = kmalloc(cap);
    if (!d) return -12;  // ENOMEM
    kmemcpy(d, ip->data, ip->size);
    if (!ip->borrowed) kfree(ip->data);
    ip->data = d;
    ip->cap = cap;
    ip->borrowed = 0;
    return 0;
}

void inode_truncate(struct inode *ip) {
    if (!ip->borrowed) kfree(ip->data);
    ip->data = 0;
    ip->size = ip->cap = 0;
    ip->borrowed = 0;
}

// 開いている人がいなくなり、どのディレクトリからも外されていれば捨てる
void inode_put(struct inode *ip) {
    if (--ip->refs > 0 || !ip->unlinked) return;
    inode_truncate(ip);
    kfree(ip);
}

// path をたどる。base は相対パスの起点。follow が 1 なら最後のシンボリックリンクもたどる
struct inode *vfs_lookup_at(struct inode *base, const char *path, int follow, long *err) {
    struct inode *ip = path[0] == '/' ? vfs_root : base;
    int hops = 0;
    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *name = p;
        while (*p && *p != '/') p++;
        u64 len = p - name;
        if (ip->type != I_DIR) { *err = -20; return 0; }  // ENOTDIR
        struct inode *next = dir_lookup(ip, name, len);
        if (!next) { *err = -2; return 0; }  // ENOENT
        int last = !*p || (p[0] == '/' && !p[1]);
        if (next->type == I_LNK && (!last || follow)) {
            if (++hops > 8) { *err = -40; return 0; }  // ELOOP
            char target[256];
            u64 n = next->size < 255 ? next->size : 255;
            kmemcpy(target, next->data, n);
            target[n] = 0;
            next = vfs_lookup_at(ip, target, 1, err);
            if (!next) return 0;
        }
        ip = next;
    }
    return ip;
}

// path の親ディレクトリと、最後の名前（*name、*len）を返す
struct inode *vfs_lookup_parent(struct inode *base, const char *path, const char **name, u64 *len, long *err) {
    const char *end = path;
    while (*end) end++;
    while (end > path && end[-1] == '/') end--;
    const char *start = end;
    while (start > path && start[-1] != '/') start--;
    if (start == end) { *err = -17; return 0; }  // "/" や "" 自体（EEXIST として扱う）
    char dir[256];
    u64 n = start - path;
    if (n >= sizeof dir) { *err = -36; return 0; }
    kmemcpy(dir, path, n);
    dir[n] = 0;
    struct inode *d = n ? vfs_lookup_at(base, dir, 1, err) : (path[0] == '/' ? vfs_root : base);
    if (!d) return 0;
    if (d->type != I_DIR) { *err = -20; return 0; }
    *name = start;
    *len = end - start;
    return d;
}

// ---- initramfs から作る ----

static u64 hex8(const u8 *p) {
    u64 v = 0;
    for (int i = 0; i < 8; i++) {
        u8 c = p[i];
        v = v * 16 + (c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
    }
    return v;
}

// "a/b/c" の親（a/b）を、無ければ作りながらたどる
static struct inode *make_parents(const char *path, const char **last, u64 *len) {
    struct inode *d = vfs_root;
    const char *p = path;
    for (;;) {
        while (*p == '/') p++;
        const char *name = p;
        while (*p && *p != '/') p++;
        if (!*p) { *last = name; *len = p - name; return d; }
        struct inode *next = dir_lookup(d, name, p - name);
        if (!next) {
            next = inode_new(I_DIR, 0755);
            dir_add(d, next, name, p - name);
        }
        d = next;
    }
}

void vfs_init(const u8 *cpio, u64 total) {
    vfs_root = inode_new(I_DIR, 0755);
    u64 off = 0;
    while (cpio && off + 110 <= total) {
        const u8 *h = cpio + off;
        if (h[0] != '0' || h[5] != '1') break;  // "070701" のはず
        u32 mode = hex8(h + 14);
        u64 filesize = hex8(h + 54), namesize = hex8(h + 94);
        const char *name = (const char *)h + 110;
        u64 data = (off + 110 + namesize + 3) & ~3UL;
        off = (data + filesize + 3) & ~3UL;
        if (streq(name, "TRAILER!!!")) break;
        if (streq(name, ".") || !name[0]) continue;
        const char *last;
        u64 len;
        struct inode *d = make_parents(name, &last, &len);
        if (!len) continue;
        struct inode *ip = dir_lookup(d, last, len);
        int type = (mode & 0170000) == 0040000 ? I_DIR : (mode & 0170000) == 0120000 ? I_LNK
                 : (mode & 0170000) == 0020000 ? I_CHR : I_REG;
        if (!ip) {
            ip = inode_new(type, mode);
            dir_add(d, ip, last, len);
        }
        ip->mode = mode & 07777;
        if (type == I_REG || type == I_LNK) {
            ip->data = (u8 *)cpio + data;
            ip->size = filesize;
            ip->borrowed = 1;
        }
        if (type == I_CHR) ip->rdev = hex8(h + 78) << 8 | hex8(h + 86);
    }
}
