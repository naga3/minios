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

// serial.c
void serial_init(void);
void serial_putc(char c);
void kprint(const char *s);

// QEMU のデバッグ用の終了装置（isa-debug-exit、ポート 0xf4）で QEMU を終わらせる。QEMU の終了コードは (code << 1) | 1
static inline void __attribute__((noreturn)) qemu_exit(u8 code) {
    outb(0xf4, code);
    for (;;) __asm__ volatile("cli; hlt");
}
