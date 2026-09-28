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
u64 mm_free_pages(void);
u64 *pml4_new(void);                            // カーネルの部分だけを持つ、新しいプロセスのページテーブル（高い番地で返す）
int map_page(u64 *pml4, u64 va, u64 pa, u64 flags);
void *user_ptr(u64 *pml4, u64 va);              // プロセスの番地 va を、カーネルから触れる番地に（写されていなければ 0）
void kmemset(void *d, int c, size_t n);
void kmemcpy(void *d, const void *s, size_t n);

// initrd.c: initramfs（cpio）
void initrd_init(const void *data, u64 size);
const void *initrd_find(const char *path, u64 *size, u32 *mode);

// proc.c: プロセス
extern u64 *current_pml4;
extern long current_pid;
long exec_initial(const char *path, char *const argv[], char *const envp[]);
void process_exit(long code);

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
