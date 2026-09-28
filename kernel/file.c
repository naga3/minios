// ファイル記述子と、ファイルのシステムコール。番号・引数・返り値は Linux（x86_64）と同じ
#include "kernel.h"

#define O_ACCMODE   3
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     0100
#define O_EXCL      0200
#define O_TRUNC     01000
#define O_APPEND    02000
#define O_DIRECTORY 0200000

// ---- ユーザーのメモリとのやりとり ----

long copy_in(void *dst, u64 va, u64 n) {
    for (u64 i = 0; i < n; i++) {
        u8 *p = user_ptr(cur->pml4, va + i);
        if (!p) return -14;  // EFAULT
        ((u8 *)dst)[i] = *p;
    }
    return 0;
}

long copy_out(u64 va, const void *src, u64 n) {
    for (u64 i = 0; i < n; i++) {
        u8 *p = user_ptr(cur->pml4, va + i);
        if (!p) return -14;
        *p = ((const u8 *)src)[i];
    }
    return 0;
}

long copy_str_in(char *dst, u64 va, u64 max) {
    for (u64 i = 0; i < max; i++) {
        char *p = user_ptr(cur->pml4, va + i);
        if (!p) return -14;
        dst[i] = *p;
        if (!*p) return i;
    }
    return -36;  // ENAMETOOLONG
}

// ---- ファイル ----

struct file *file_new(int kind) {
    struct file *f = kmalloc(sizeof *f);
    if (!f) return 0;
    f->kind = kind;
    f->refs = 1;
    return f;
}

struct file *file_dup(struct file *f) {
    f->refs++;
    return f;
}

void file_close(struct file *f) {
    if (--f->refs > 0) return;
    if (f->kind == F_INODE) inode_put(f->ip);
    if (f->kind == F_PIPE_R || f->kind == F_PIPE_W) pipe_close(f->pipe, f->kind == F_PIPE_W);
    kfree(f);
}

static int fd_alloc(struct file *f, int from) {
    for (int i = from; i < NFD; i++)
        if (!cur->fds[i]) {
            cur->fds[i] = f;
            return i;
        }
    return -24;  // EMFILE
}

static struct file *fd_get(long fd) {
    if (fd < 0 || fd >= NFD) return 0;
    return cur->fds[fd];
}

// デバイス番号（主番号 << 8 | 副番号）から、開いたときの種類を決める
static int device_kind(u32 rdev) {
    switch (rdev) {
    case 5 << 8 | 1:   // /dev/console
    case 5 << 8 | 0:   // /dev/tty
    case 4 << 8 | 64:  // /dev/ttyS0
        return F_TTY;
    case 1 << 8 | 3:   // /dev/null
        return F_NULL;
    case 1 << 8 | 5:   // /dev/zero
        return F_ZERO;
    }
    return -1;
}

long sys_open(u64 path_va, long flags, long mode) {
    char path[256];
    long e = copy_str_in(path, path_va, sizeof path);
    if (e < 0) return e;
    struct inode *ip = vfs_lookup_at(cur->cwd, path, 1, &e);
    if (!ip) {
        if (e != -2 || !(flags & O_CREAT)) return e;
        const char *name;
        u64 len;
        struct inode *dir = vfs_lookup_parent(cur->cwd, path, &name, &len, &e);
        if (!dir) return e;
        ip = inode_new(I_REG, mode & ~0);
        if (!ip) return -12;
        dir_add(dir, ip, name, len);
    } else if ((flags & O_CREAT) && (flags & O_EXCL)) {
        return -17;  // EEXIST
    }
    if (ip->type == I_DIR && (flags & O_ACCMODE) != 0) return -21;  // EISDIR
    if ((flags & O_DIRECTORY) && ip->type != I_DIR) return -20;       // ENOTDIR
    struct file *f;
    if (ip->type == I_CHR) {
        int kind = device_kind(ip->rdev);
        if (kind < 0) return -6;  // ENXIO
        f = file_new(kind);
    } else {
        f = file_new(F_INODE);
        if (!f) return -12;
        f->ip = ip;
        ip->refs++;
        if ((flags & O_TRUNC) && ip->type == I_REG && (flags & O_ACCMODE)) inode_truncate(ip);
    }
    if (!f) return -12;
    f->flags = flags;
    int fd = fd_alloc(f, 0);
    if (fd < 0) file_close(f);
    return fd;
}

long sys_close(long fd) {
    struct file *f = fd_get(fd);
    if (!f) return -9;  // EBADF
    cur->fds[fd] = 0;
    file_close(f);
    return 0;
}

long sys_read(long fd, u64 buf, u64 n) {
    struct file *f = fd_get(fd);
    if (!f || (f->flags & O_ACCMODE) == O_WRONLY) return -9;
    switch (f->kind) {
    case F_TTY:    return tty_read(buf, n);
    case F_NULL:   return 0;
    case F_ZERO:
        for (u64 i = 0; i < n; i++)
            if (copy_out(buf + i, "", 1) < 0) return -14;
        return n;
    case F_PIPE_R: return pipe_read(f->pipe, buf, n);
    case F_PIPE_W: return -9;
    }
    struct inode *ip = f->ip;
    if (ip->type == I_DIR) return -21;  // EISDIR
    if (f->off >= ip->size) return 0;
    if (n > ip->size - f->off) n = ip->size - f->off;
    if (copy_out(buf, ip->data + f->off, n) < 0) return -14;
    f->off += n;
    return n;
}

long sys_write(long fd, u64 buf, u64 n) {
    struct file *f = fd_get(fd);
    if (!f || ((f->flags & O_ACCMODE) == 0 && f->kind == F_INODE)) return -9;  // 読むだけで開いたファイル
    switch (f->kind) {
    case F_TTY:    return tty_write(buf, n);
    case F_NULL:
    case F_ZERO:   return n;
    case F_PIPE_W: return pipe_write(f->pipe, buf, n);
    case F_PIPE_R: return -9;
    }
    struct inode *ip = f->ip;
    if (ip->type != I_REG) return -21;
    if (f->flags & O_APPEND) f->off = ip->size;
    long e = inode_reserve(ip, f->off + n);
    if (e < 0) return e;
    if (f->off > ip->size) kmemset(ip->data + ip->size, 0, f->off - ip->size);
    if (copy_in(ip->data + f->off, buf, n) < 0) return -14;
    f->off += n;
    if (f->off > ip->size) ip->size = f->off;
    return n;
}

long sys_lseek(long fd, long off, long whence) {
    struct file *f = fd_get(fd);
    if (!f) return -9;
    if (f->kind != F_INODE) return -29;  // ESPIPE
    long base = whence == 0 ? 0 : whence == 1 ? (long)f->off : whence == 2 ? (long)f->ip->size : -1;
    if (base < 0 || base + off < 0) return -22;
    f->off = base + off;
    return f->off;
}

// Linux の struct stat（x86_64）
struct kstat {
    u64 dev, ino, nlink;
    u32 mode, uid, gid, pad0;
    u64 rdev;
    long size, blksize, blocks;
    u64 atime, atime_ns, mtime, mtime_ns, ctime, ctime_ns;
    long unused[3];
};

static const u32 type_bits[] = {[I_DIR] = 0040000, [I_REG] = 0100000, [I_LNK] = 0120000, [I_CHR] = 0020000};

static long stat_out(struct inode *ip, u64 va) {
    struct kstat st;
    kmemset(&st, 0, sizeof st);
    st.ino = ip->ino;
    st.nlink = 1;
    st.mode = type_bits[ip->type] | ip->mode;
    st.rdev = ip->type == I_CHR ? ((ip->rdev >> 8) << 8 | (ip->rdev & 0xff)) : 0;
    st.size = ip->type == I_CHR ? 0 : ip->size;
    st.blksize = 4096;
    st.blocks = (ip->size + 511) / 512;
    return copy_out(va, &st, sizeof st);
}

long sys_stat(u64 path_va, u64 st_va, int follow) {
    char path[256];
    long e = copy_str_in(path, path_va, sizeof path);
    if (e < 0) return e;
    struct inode *ip = vfs_lookup_at(cur->cwd, path, follow, &e);
    if (!ip) return e;
    return stat_out(ip, st_va);
}

long sys_fstat(long fd, u64 st_va) {
    struct file *f = fd_get(fd);
    if (!f) return -9;
    if (f->kind == F_INODE) return stat_out(f->ip, st_va);
    struct inode dev = {.type = f->kind == F_TTY ? I_CHR : I_REG, .mode = 0666};
    return stat_out(&dev, st_va);
}

long sys_dup(long fd) {
    struct file *f = fd_get(fd);
    if (!f) return -9;
    int n = fd_alloc(f, 0);
    if (n >= 0) file_dup(f);
    return n;
}

long sys_dup2(long fd, long to) {
    struct file *f = fd_get(fd);
    if (!f || to < 0 || to >= NFD) return -9;
    if (fd == to) return to;
    if (cur->fds[to]) file_close(cur->fds[to]);
    cur->fds[to] = file_dup(f);
    return to;
}

long sys_getdents64(long fd, u64 buf, u64 n) {
    struct file *f = fd_get(fd);
    if (!f || f->kind != F_INODE) return -9;
    struct inode *dir = f->ip;
    if (dir->type != I_DIR) return -20;
    static const u8 dtype[] = {[I_DIR] = 4, [I_REG] = 8, [I_LNK] = 10, [I_CHR] = 2};
    u64 used = 0, index = 0;
    // 0 番目は "."、1 番目は ".."、その後に子を名前の順に。f->off は次に返す番号
    struct inode *c = dir->child;
    for (;; index++) {
        const char *name;
        struct inode *ip;
        if (index == 0) name = ".", ip = dir;
        else if (index == 1) name = "..", ip = dir->parent ? dir->parent : dir;
        else {
            if (!c) break;
            ip = c, name = c->name;
            c = c->next;
        }
        if (index < f->off) continue;
        u64 len = 0;
        while (name[len]) len++;
        u64 reclen = (19 + len + 1 + 7) & ~7UL;
        if (used + reclen > n) {
            if (!used) return -22;  // 1 つも入らない
            break;
        }
        u8 rec[19 + NAME_MAX + 8];
        kmemset(rec, 0, reclen);
        *(u64 *)rec = ip->ino;
        *(long *)(rec + 8) = index + 1;
        *(u16 *)(rec + 16) = reclen;
        rec[18] = dtype[ip->type];
        kmemcpy(rec + 19, name, len);
        if (copy_out(buf + used, rec, reclen) < 0) return -14;
        used += reclen;
        f->off = index + 1;
    }
    return used;
}

long sys_mkdir(u64 path_va, long mode) {
    char path[256];
    long e = copy_str_in(path, path_va, sizeof path);
    if (e < 0) return e;
    const char *name;
    u64 len;
    struct inode *dir = vfs_lookup_parent(cur->cwd, path, &name, &len, &e);
    if (!dir) return e;
    if (dir_lookup(dir, name, len)) return -17;  // EEXIST
    struct inode *ip = inode_new(I_DIR, mode);
    if (!ip) return -12;
    dir_add(dir, ip, name, len);
    return 0;
}

static long remove(u64 path_va, int want_dir) {
    char path[256];
    long e = copy_str_in(path, path_va, sizeof path);
    if (e < 0) return e;
    const char *name;
    u64 len;
    struct inode *dir = vfs_lookup_parent(cur->cwd, path, &name, &len, &e);
    if (!dir) return e;
    struct inode *ip = dir_lookup(dir, name, len);
    if (!ip) return -2;
    if (want_dir && ip->type != I_DIR) return -20;  // ENOTDIR
    if (!want_dir && ip->type == I_DIR) return -21; // EISDIR
    if (want_dir && ip->child) return -39;          // ENOTEMPTY
    if (ip == cur->cwd || ip == vfs_root) return -16;  // EBUSY
    dir_remove(dir, ip);
    ip->unlinked = 1;
    ip->refs++;
    inode_put(ip);  // 開いている人がいなければ、ここで捨てる
    return 0;
}

long sys_unlink(u64 path_va) { return remove(path_va, 0); }
long sys_rmdir(u64 path_va) { return remove(path_va, 1); }

long sys_chdir(u64 path_va) {
    char path[256];
    long e = copy_str_in(path, path_va, sizeof path);
    if (e < 0) return e;
    struct inode *ip = vfs_lookup_at(cur->cwd, path, 1, &e);
    if (!ip) return e;
    if (ip->type != I_DIR) return -20;
    cur->cwd = ip;
    return 0;
}

long sys_getcwd(u64 buf, u64 size) {
    char tmp[256];
    u64 n = sizeof tmp;
    tmp[--n] = 0;
    for (struct inode *ip = cur->cwd; ip && ip != vfs_root; ip = ip->parent) {
        u64 len = 0;
        while (ip->name[len]) len++;
        if (n < len + 1) return -36;
        n -= len;
        kmemcpy(tmp + n, ip->name, len);
        tmp[--n] = '/';
    }
    if (tmp[n] == 0) tmp[--n] = '/';
    u64 len = sizeof tmp - n;
    if (len > size) return -34;  // ERANGE
    if (copy_out(buf, tmp + n, len) < 0) return -14;
    return len;
}

long sys_chmod(u64 path_va, long mode) {
    char path[256];
    long e = copy_str_in(path, path_va, sizeof path);
    if (e < 0) return e;
    struct inode *ip = vfs_lookup_at(cur->cwd, path, 1, &e);
    if (!ip) return e;
    ip->mode = mode & 07777;
    return 0;
}

// fork: 子に開いているファイルを引き継ぐ。exit: 全部閉じる
void files_fork(struct proc *from, struct proc *to) {
    for (int i = 0; i < NFD; i++) to->fds[i] = from->fds[i] ? file_dup(from->fds[i]) : 0;
    to->cwd = from->cwd;
}

void files_close_all(struct proc *p) {
    for (int i = 0; i < NFD; i++)
        if (p->fds[i]) {
            file_close(p->fds[i]);
            p->fds[i] = 0;
        }
}
