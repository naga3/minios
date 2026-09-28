// プロセス: ELF を読み込み、スタックに argc・argv・envp を積んで、ユーザーモードで動かす（今は 1 つだけ）
#include "kernel.h"

u64 *current_pml4;
long current_pid;

extern void enter_user(struct regs *r);
extern u64 kernel_rsp;

#define USER_STACK_TOP 0x40000000UL  // 1GB の手前から下へ
#define USER_STACK_PAGES 16

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

static u8 kstack[16384] __attribute__((aligned(16)));  // システムコールと割り込みのときのカーネルのスタック

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

// スタックに文字列を積み、その番地を返す
static u64 push_string(u64 *pml4v, u64 *sp, const char *s) {
    u64 n = 0;
    while (s[n]) n++;
    *sp -= n + 1;
    for (u64 i = 0; i <= n; i++) *(char *)user_ptr(pml4v, *sp + i) = s[i];
    return *sp;
}

static void push_u64(u64 *pml4v, u64 *sp, u64 v) {
    *sp -= 8;
    *(u64 *)user_ptr(pml4v, *sp) = v;
}

// 最初のプロセスを起動する（戻らない）。失敗したら負のエラー番号を返す
long exec_initial(const char *path, char *const argv[], char *const envp[]) {
    u64 size;
    u32 mode;
    const u8 *data = initrd_find(path, &size, &mode);
    if (!data) return -2;
    u64 *pml4v = pml4_new();
    u64 entry = load_elf(pml4v, data, size);
    if (!entry) return -8;  // ENOEXEC
    for (u64 i = 1; i <= USER_STACK_PAGES; i++) map_page(pml4v, USER_STACK_TOP - i * PAGE, page_alloc(), PTE_W | PTE_U);

    // Linux と同じ形: [argc][argv...][0][envp...][0][auxv（AT_NULL）]。文字列は上に置く
    u64 sp = USER_STACK_TOP, argp[64], envpp[64];
    int argc = 0, envc = 0;
    while (argv[argc] && argc < 63) argc++;
    while (envp[envc] && envc < 63) envc++;
    for (int i = argc - 1; i >= 0; i--) argp[i] = push_string(pml4v, &sp, argv[i]);
    for (int i = envc - 1; i >= 0; i--) envpp[i] = push_string(pml4v, &sp, envp[i]);
    sp &= ~15UL;
    if ((argc + envc + 5) % 2) push_u64(pml4v, &sp, 0);  // argc を積んだところで 16 バイト境界になるように
    push_u64(pml4v, &sp, 0);  // AT_NULL
    push_u64(pml4v, &sp, 0);
    push_u64(pml4v, &sp, 0);
    for (int i = envc - 1; i >= 0; i--) push_u64(pml4v, &sp, envpp[i]);
    push_u64(pml4v, &sp, 0);
    for (int i = argc - 1; i >= 0; i--) push_u64(pml4v, &sp, argp[i]);
    push_u64(pml4v, &sp, argc);

    current_pml4 = pml4v;
    current_pid = 1;
    kernel_rsp = (u64)kstack + sizeof kstack;
    cpu_set_kernel_stack(kernel_rsp);
    __asm__ volatile("mov %0, %%cr3" ::"r"(V2P(pml4v)));
    static struct regs r;
    r = (struct regs){.rip = entry, .cs = 0x23, .rflags = 0x202, .rsp = sp, .ss = 0x1b};
    enter_user(&r);
    return 0;
}

void process_exit(long code) {
    kprint("[process "); kprint_u(current_pid); kprint(" exited with "); kprint_u(code & 0xff); kprint("]\n");
    qemu_exit(0);
}
