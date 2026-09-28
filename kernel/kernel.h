// 自作カーネルの共通の宣言
#pragma once

typedef unsigned long size_t;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long u64;

static inline void outb(u16 port, u8 v) { __asm__ volatile("outb %0, %1" ::"a"(v), "Nd"(port)); }
static inline u8 inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

// 割り込みのときに isr.S が積むもの（積んだ順の逆）
struct regs {
    u64 r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 vector, error;
    u64 rip, cs, rflags, rsp, ss;  // CPU が積む
};

// serial.c
void serial_init(void);
void serial_putc(char c);
void serial_irq(void);
extern int serial_to_tty;
int serial_getc(void);  // 受け取った文字。無ければ -1
void kprint(const char *s);
void kprint_u(u64 v);
void kprint_hex(u64 v);

// cpu.c
void cpu_init(void);
void cpu_set_kernel_stack(u64 top);

// mm.c: 物理メモリ（4KB のページ）と、プロセスごとのページテーブル
#define DIRECT_MAP 0xFFFF800000000000UL          // 物理メモリを写した高い番地
#define P2V(p) ((void *)((u64)(p) + DIRECT_MAP))
#define V2P(v) ((u64)(v) - DIRECT_MAP)
#define PAGE 4096UL
#define PTE_P 0x1UL
#define PTE_W 0x2UL
#define PTE_U 0x4UL
void mm_init(u64 free_start, u64 free_end);
u64 page_alloc(void);                           // 0 で埋めた 4KB のページ（物理アドレス）。無ければ 0
void page_free(u64 pa);
u64 pages_alloc_contig(u64 n);                  // 続いた n ページ（カーネルスタック用。返さない）
u64 mm_free_pages(void);
u64 *pml4_new(void);                            // カーネルの部分だけを持つ、新しいプロセスのページテーブル（高い番地で返す）
int map_page(u64 *pml4, u64 va, u64 pa, u64 flags);
int copy_user_space(u64 *from, u64 *to);        // ユーザーのページを全部コピーする（fork）
void free_user_space(u64 *pml4);                // ユーザーのページと途中の表を返す（表の一番上は残す）
void pml4_free(u64 *pml4);                      // ページテーブルごと全部返す
void *user_ptr(u64 *pml4, u64 va);              // プロセスの番地 va を、カーネルから触れる番地に（写されていなければ 0）
void kmemset(void *d, int c, size_t n);
void kmemcpy(void *d, const void *s, size_t n);

// kmalloc.c
void *kmalloc(u64 n);  // 0 で埋めた n バイト
void kfree(void *p);

// vfs.c: メモリ上のファイルシステム
#define NAME_MAX 64
enum { I_DIR = 1, I_REG, I_LNK, I_CHR };
struct inode {
    int type;
    u32 mode;           // 権限（07777）
    u32 rdev;           // デバイス番号（主番号 << 8 | 副番号）
    u64 ino;
    char name[NAME_MAX];
    struct inode *parent, *child, *next;  // ディレクトリの木（子は名前の順）
    u8 *data;           // ファイルの中身（シンボリックリンクなら指す先）
    u64 size, cap;
    int borrowed;       // data が initramfs を指している（書き換えるときにコピーする）
    int refs;           // 開いている数
    int unlinked;       // どのディレクトリからも外された
};
extern struct inode *vfs_root;
void vfs_init(const u8 *cpio, u64 size);
struct inode *inode_new(int type, u32 mode);
struct inode *dir_lookup(struct inode *dir, const char *name, u64 len);
void dir_add(struct inode *dir, struct inode *ip, const char *name, u64 len);
void dir_remove(struct inode *dir, struct inode *ip);
int inode_reserve(struct inode *ip, u64 need);
void inode_truncate(struct inode *ip);
void inode_put(struct inode *ip);
struct inode *vfs_lookup_at(struct inode *base, const char *path, int follow, long *err);
struct inode *vfs_lookup_parent(struct inode *base, const char *path, const char **name, u64 *len, long *err);

// file.c: ファイル記述子
#define NFD 32
enum { F_INODE = 1, F_TTY, F_NULL, F_ZERO, F_PIPE_R, F_PIPE_W };
struct pipe;
struct file {
    int kind, refs;
    long flags;
    struct inode *ip;
    u64 off;
    struct pipe *pipe;
};
struct proc;
long copy_in(void *dst, u64 va, u64 n);
long copy_out(u64 va, const void *src, u64 n);
long copy_str_in(char *dst, u64 va, u64 max);
struct file *file_new(int kind);
struct file *file_dup(struct file *f);
void file_close(struct file *f);
void files_fork(struct proc *from, struct proc *to);
void files_close_all(struct proc *p);
long sys_open(u64 path, long flags, long mode);
long sys_close(long fd);
long sys_read(long fd, u64 buf, u64 n);
long sys_write(long fd, u64 buf, u64 n);
long sys_lseek(long fd, long off, long whence);
long sys_stat(u64 path, u64 st, int follow);
long sys_fstat(long fd, u64 st);
long sys_dup(long fd);
long sys_dup2(long fd, long to);
long sys_getdents64(long fd, u64 buf, u64 n);
long sys_mkdir(u64 path, long mode);
long sys_unlink(u64 path);
long sys_rmdir(u64 path);
long sys_chdir(u64 path);
long sys_getcwd(u64 buf, u64 size);
long sys_chmod(u64 path, long mode);
long sys_ioctl(long fd, long cmd, u64 arg);

// tty.c・pipe.c
extern long tty_fg_pgrp;
void tty_input(char c);
long tty_read(u64 buf, u64 n);
long tty_write(u64 buf, u64 n);
long tty_ioctl(long cmd, u64 arg);
long sys_pipe(u64 fds);
long pipe_read(struct pipe *p, u64 buf, u64 n);
long pipe_write(struct pipe *p, u64 buf, u64 n);
void pipe_close(struct pipe *p, int writer);

// proc.c: プロセス
enum { UNUSED, EMBRYO, RUNNABLE, RUNNING, SLEEPING, ZOMBIE };
struct proc {
    int state;
    long pid, ppid, pgid, sid;
    int ctty;          // 端末を制御端末にしている
    u64 *pml4;         // ページテーブル（高い番地）
    u64 kstack_top;    // カーネルスタックの一番上
    u64 ctx_rsp;       // swtch で取っておいたカーネルスタック
    long exit_status;  // wait4 の形
    u64 wake_tick;     // nanosleep: この ticks になったら起きる（0 なら無し）
    void *chan;        // 眠っている理由（wakeup の相手）
    struct file *fds[NFD];
    struct inode *cwd;
    u64 sig_pending, sig_ignored;
    char name[16];     // ps に出す名前（prctl の PR_SET_NAME か、exec したファイルの名前）
};
#define NPROC 64
extern struct proc procs[NPROC];
extern struct proc *cur;
long exec_initial(const char *path, char *const argv[], char *const envp[]);
void schedule(void);
void sleep_on(void *chan);
void wakeup(void *chan);
void process_exit(long code);
void process_exit_status(long status);
long sys_fork(struct regs *r);
long sys_execve(struct regs *r);
long sys_wait4(long pid, u64 status_va, long options);
long sys_nanosleep(u64 ts_va);
long sys_setsid(void);
void set_name(struct proc *p, const char *path);
long sys_setpgid(long pid, long pgid);

// signal.c
void signal_send(struct proc *p, int sig);
void signal_pgrp(long pgrp, int sig);
int signal_pending(struct proc *p);
void signal_check(void);
long sys_rt_sigaction(long sig, u64 act, u64 old);
long sys_kill(long pid, long sig);

// syscall.c
void syscall_init(void);

// irq.c
void irq_init(void);
extern volatile u64 ticks;  // タイマー（100Hz）の回数

// QEMU のデバッグ用の終了装置（isa-debug-exit、ポート 0xf4）で QEMU を終わらせる。QEMU の終了コードは (code << 1) | 1
static inline void __attribute__((noreturn)) qemu_exit(u8 code) {
    outb(0xf4, code);
    for (;;) __asm__ volatile("cli; hlt");
}
