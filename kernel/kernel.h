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

// irq.c
void irq_init(void);
extern volatile u64 ticks;  // タイマー（100Hz）の回数

// QEMU のデバッグ用の終了装置（isa-debug-exit、ポート 0xf4）で QEMU を終わらせる。QEMU の終了コードは (code << 1) | 1
static inline void __attribute__((noreturn)) qemu_exit(u8 code) {
    outb(0xf4, code);
    for (;;) __asm__ volatile("cli; hlt");
}
