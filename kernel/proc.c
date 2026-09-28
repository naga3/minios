// プロセス: 表、スケジューラ、fork・execve・exit・wait4・nanosleep
#include "kernel.h"

extern void enter_user(struct regs *r);
extern void swtch(u64 *old_rsp, u64 new_rsp);
extern void proc_trampoline(void);
extern u64 kernel_rsp;

#define NPROC 64
#define KSTACK_PAGES 4
#define USER_STACK_TOP 0x40000000UL  // 1GB の手前から下へ
#define USER_STACK_PAGES 16

struct proc procs[NPROC];
struct proc *cur;
static long next_pid = 1;

// ---- 割り込みの禁止と復元 ----

static u64 irq_save(void) {
    u64 f;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(f));
    return f;
}

static void irq_restore(u64 f) { __asm__ volatile("push %0; popfq" ::"r"(f) : "memory", "cc"); }

// ---- スケジューラ ----

static void switch_to(struct proc *next) {
    struct proc *old = cur;
    cur = next;
    next->state = RUNNING;
    kernel_rsp = next->kstack_top;
    cpu_set_kernel_stack(next->kstack_top);
    __asm__ volatile("mov %0, %%cr3" ::"r"(V2P(next->pml4)) : "memory");
    swtch(&old->ctx_rsp, next->ctx_rsp);
}

// 次に動くプロセスを選んで切り替える。今のプロセスが動けるなら、他が無ければそのまま戻る
void schedule(void) {
    u64 f = irq_save();
    for (;;) {
        for (int i = 0; i < NPROC; i++)  // 眠りの時間が来たものを起こす
            if (procs[i].state == SLEEPING && procs[i].wake_tick && procs[i].wake_tick <= ticks) {
                procs[i].state = RUNNABLE;
                procs[i].wake_tick = 0;
            }
        int start = cur - procs;
        for (int k = 1; k <= NPROC; k++) {
            struct proc *p = &procs[(start + k) % NPROC];
            if (p->state == RUNNABLE) {
                if (cur->state == RUNNING) cur->state = RUNNABLE;
                if (p != cur) switch_to(p);
                else cur->state = RUNNING;
                irq_restore(f);
                return;
            }
        }
        if (cur->state == RUNNING) break;  // 他に動けるものが無い。今のまま続ける
        __asm__ volatile("sti; hlt; cli");  // 誰も動けない。次の割り込み（タイマー、キー入力）を待つ
    }
    irq_restore(f);
}

void sleep_on(void *chan) {
    cur->chan = chan;
    cur->state = SLEEPING;
    schedule();
    cur->chan = 0;
}

void wakeup(void *chan) {
    for (int i = 0; i < NPROC; i++)
        if (procs[i].state == SLEEPING && procs[i].chan == chan) procs[i].state = RUNNABLE;
}

// ---- プロセスの用意 ----

static struct proc *proc_alloc(void) {
    for (int i = 0; i < NPROC; i++) {
        struct proc *p = &procs[i];
        if (p->state != UNUSED) continue;
        if (!p->kstack_top) {  // カーネルスタックは使い回す
            u64 base = pages_alloc_contig(KSTACK_PAGES);
            if (!base) return 0;
            p->kstack_top = (u64)P2V(base) + KSTACK_PAGES * PAGE;
        }
        p->pid = next_pid++;
        p->state = EMBRYO;
        p->wake_tick = 0;
        p->chan = 0;
        return p;
    }
    return 0;
}

// ユーザーモードの状態 r（カーネルスタックの一番上に写す）から動き始めるよう、切り替えの準備をする
static void proc_prepare(struct proc *p, const struct regs *r) {
    struct regs *top = (struct regs *)(p->kstack_top - sizeof *top);
    *top = *r;
    u64 *sp = (u64 *)top;
    *--sp = (u64)proc_trampoline;  // swtch の ret の行き先
    *--sp = 0;                     // rbp
    *--sp = 0;                     // rbx
    *--sp = (u64)top;              // r12: proc_trampoline に渡す struct regs *
    *--sp = 0;                     // r13
    *--sp = 0;                     // r14
    *--sp = 0;                     // r15
    p->ctx_rsp = (u64)sp;
}

struct elf64_ehdr {
    u8 ident[16];
    u16 type, machine;
    u32 version;
    u64 entry, phoff, shoff;
    u32 flags;
    u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct elf64_phdr {
    u32 type, flags;
    u64 offset, vaddr, paddr, filesz, memsz, align;
};

// ELF を pml4 に読み込み、入り口の番地を返す。失敗なら 0
static u64 load_elf(u64 *pml4v, const u8 *data, u64 size) {
    const struct elf64_ehdr *eh = (const void *)data;
    if (size < sizeof *eh || data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' || data[3] != 'F' ||
        eh->machine != 62 /* x86_64 */)
        return 0;
    for (int i = 0; i < eh->phnum; i++) {
        const struct elf64_phdr *ph = (const void *)(data + eh->phoff + i * eh->phentsize);
        if (ph->type != 1 /* PT_LOAD */) continue;
        u64 start = ph->vaddr & ~(PAGE - 1), end = (ph->vaddr + ph->memsz + PAGE - 1) & ~(PAGE - 1);
        for (u64 va = start; va < end; va += PAGE) {
            if (user_ptr(pml4v, va)) continue;  // 前の部分と同じページ
            u64 pa = page_alloc();
            if (!pa || map_page(pml4v, va, pa, PTE_W | PTE_U) < 0) return 0;
        }
        for (u64 j = 0; j < ph->filesz; j++) *(u8 *)user_ptr(pml4v, ph->vaddr + j) = data[ph->offset + j];
    }
    return eh->entry;
}

static void push_bytes(u64 *pml4v, u64 *sp, const void *src, u64 n) {
    *sp -= n;
    for (u64 i = 0; i < n; i++) *(u8 *)user_ptr(pml4v, *sp + i) = ((const u8 *)src)[i];
}

static void push_u64(u64 *pml4v, u64 *sp, u64 v) { push_bytes(pml4v, sp, &v, 8); }

#define MAXARG 256
#define ARGBUF 16384

// 新しいアドレス空間に path の ELF を読み込み、スタックに Linux と同じ形で argc・argv・envp を積む。
// argv・envp はカーネルの中の文字列。成功なら 0 で、*pml4_out・*entry・*sp に結果
static long build_image(const char *path, char **argv, int argc, char **envp, int envc,
                        u64 **pml4_out, u64 *entry, u64 *sp_out) {
    u64 size;
    u32 mode;
    const u8 *data = initrd_find(path, &size, &mode);
    if (!data) return -2;  // ENOENT
    u64 *pml4v = pml4_new();
    *entry = load_elf(pml4v, data, size);
    if (!*entry) {
        pml4_free(pml4v);
        return -8;  // ENOEXEC
    }
    for (u64 i = 1; i <= USER_STACK_PAGES; i++) map_page(pml4v, USER_STACK_TOP - i * PAGE, page_alloc(), PTE_W | PTE_U);

    // [argc][argv...][0][envp...][0][auxv（AT_NULL）]。文字列は上に置く
    static u64 argp[MAXARG], envpp[MAXARG];
    u64 sp = USER_STACK_TOP;
    for (int i = argc - 1; i >= 0; i--) {
        u64 n = 0;
        while (argv[i][n]) n++;
        push_bytes(pml4v, &sp, argv[i], n + 1);
        argp[i] = sp;
    }
    for (int i = envc - 1; i >= 0; i--) {
        u64 n = 0;
        while (envp[i][n]) n++;
        push_bytes(pml4v, &sp, envp[i], n + 1);
        envpp[i] = sp;
    }
    sp &= ~15UL;
    if ((argc + envc + 5) % 2) push_u64(pml4v, &sp, 0);  // argc を積んだところで 16 バイト境界になるように
    push_u64(pml4v, &sp, 0);  // AT_NULL
    push_u64(pml4v, &sp, 0);
    push_u64(pml4v, &sp, 0);
    for (int i = envc - 1; i >= 0; i--) push_u64(pml4v, &sp, envpp[i]);
    push_u64(pml4v, &sp, 0);
    for (int i = argc - 1; i >= 0; i--) push_u64(pml4v, &sp, argp[i]);
    push_u64(pml4v, &sp, argc);
    *pml4_out = pml4v;
    *sp_out = sp;
    return 0;
}

// 最初のプロセス（PID 1）を起動する（戻らない）。失敗したら負のエラー番号を返す
long exec_initial(const char *path, char *const argv[], char *const envp[]) {
    static struct proc boot;  // 起動の文脈（切り替えの元として 1 回だけ使う）
    int argc = 0, envc = 0;
    while (argv[argc]) argc++;
    while (envp[envc]) envc++;
    struct proc *p = proc_alloc();
    u64 entry, sp;
    long e = build_image(path, (char **)argv, argc, (char **)envp, envc, &p->pml4, &entry, &sp);
    if (e < 0) return e;
    p->ppid = 0;
    struct regs r = {.rip = entry, .cs = 0x23, .rflags = 0x202, .rsp = sp, .ss = 0x1b};
    proc_prepare(p, &r);
    p->state = RUNNABLE;
    cur = &boot;
    boot.state = UNUSED;
    schedule();
    return 0;
}

// ---- システムコール ----

long sys_fork(struct regs *r) {
    struct proc *p = proc_alloc();
    if (!p) return -11;  // EAGAIN
    p->pml4 = pml4_new();
    if (copy_user_space(cur->pml4, p->pml4) < 0) {
        pml4_free(p->pml4);
        p->state = UNUSED;
        return -12;  // ENOMEM
    }
    p->ppid = cur->pid;
    struct regs child = *r;
    child.rax = 0;  // 子の fork は 0 を返す
    proc_prepare(p, &child);
    p->state = RUNNABLE;
    return p->pid;
}

// ユーザーのメモリから文字列を取り出す（最大 max バイト）。長さか、負のエラー番号
static long copy_string_in(u64 va, char *out, u64 max) {
    for (u64 i = 0; i < max; i++) {
        char *c = user_ptr(cur->pml4, va + i);
        if (!c) return -14;  // EFAULT
        out[i] = *c;
        if (!*c) return i;
    }
    return -7;  // E2BIG
}

// 文字列の配列（argv・envp）を buf に写し、ポインタを list に並べる。数か、負のエラー番号
static long copy_strings_in(u64 va, char **list, char *buf, u64 *used) {
    int n = 0;
    for (; va; n++) {
        if (n >= MAXARG - 1) return -7;
        u64 *pp = user_ptr(cur->pml4, va + n * 8);
        if (!pp) return -14;
        if (!*pp) break;
        long len = copy_string_in(*pp, buf + *used, ARGBUF - *used);
        if (len < 0) return len;
        list[n] = buf + *used;
        *used += len + 1;
    }
    list[n] = 0;
    return n;
}

long sys_execve(struct regs *r) {
    static char path[256], buf[ARGBUF];
    static char *argv[MAXARG], *envp[MAXARG];
    long e = copy_string_in(r->rdi, path, sizeof path);
    if (e < 0) return e;
    u64 used = 0;
    long argc = copy_strings_in(r->rsi, argv, buf, &used);
    if (argc < 0) return argc;
    long envc = copy_strings_in(r->rdx, envp, buf, &used);
    if (envc < 0) return envc;
    u64 *pml4v, entry, sp;
    e = build_image(path, argv, argc, envp, envc, &pml4v, &entry, &sp);
    if (e < 0) return e;
    u64 *old = cur->pml4;
    cur->pml4 = pml4v;
    __asm__ volatile("mov %0, %%cr3" ::"r"(V2P(pml4v)) : "memory");
    pml4_free(old);
    // 戻り先を新しいプログラムの入り口にし、レジスタを空にする
    *r = (struct regs){.rip = entry, .cs = 0x23, .rflags = 0x202, .rsp = sp, .ss = 0x1b};
    return 0;
}

// 終了する（戻らない）。status は wait4 の形（下位 7 ビットがシグナル、次の 8 ビットが終了コード）
void process_exit_status(long status) {
    if (cur->pid == 1) {
        kprint("\n[init exited with "); kprint_u((status >> 8) & 0xff); kprint("]\n");
        qemu_exit(0);
    }
    free_user_space(cur->pml4);
    for (int i = 0; i < NPROC; i++)  // 子は init（PID 1）に引き取ってもらう
        if (procs[i].state != UNUSED && procs[i].ppid == cur->pid) {
            procs[i].ppid = 1;
            if (procs[i].state == ZOMBIE) wakeup(&procs[0]);
        }
    cur->exit_status = status;
    cur->state = ZOMBIE;
    for (int i = 0; i < NPROC; i++)
        if (procs[i].pid == cur->ppid && procs[i].state != UNUSED) wakeup(&procs[i]);
    schedule();
    for (;;) {}  // ゾンビは二度と動かない
}

void process_exit(long code) { process_exit_status((code & 0xff) << 8); }

#define WNOHANG 1

long sys_wait4(long pid, u64 status_va, long options) {
    for (;;) {
        int children = 0;
        for (int i = 0; i < NPROC; i++) {
            struct proc *p = &procs[i];
            if (p->state == UNUSED || p->ppid != cur->pid || (pid > 0 && p->pid != pid)) continue;
            children++;
            if (p->state != ZOMBIE) continue;
            if (status_va) {
                int *s = user_ptr(cur->pml4, status_va);
                if (!s) return -14;
                *s = p->exit_status;
            }
            long got = p->pid;
            pml4_free(p->pml4);
            p->state = UNUSED;
            p->pid = 0;
            return got;
        }
        if (!children) return -10;  // ECHILD
        if (options & WNOHANG) return 0;
        sleep_on(cur);  // 子が終わると wakeup(親) で起こされる
    }
}

long sys_nanosleep(u64 ts_va) {
    long *ts = user_ptr(cur->pml4, ts_va);
    if (!ts) return -14;
    u64 t = ts[0] * 100 + (ts[1] + 9999999) / 10000000;  // 10ms 単位に切り上げ
    if (t == 0) return 0;
    cur->wake_tick = ticks + t;
    sleep_on(0);
    return 0;
}
