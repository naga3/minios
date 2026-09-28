// CPU の表: GDT（セグメント）、TSS（ユーザーモードから割り込みが来たときのカーネルのスタック）、IDT（割り込みの入り口）
#include "kernel.h"

// セグメントの並びは syscall/sysret の決まりに合わせる（STAR: カーネル 0x08、ユーザーの基準 0x10 → SS 0x18、CS 0x20）
static u64 gdt[7] = {
    0,
    0x00af9a000000ffff,  // 0x08 カーネルのコード（64 ビット）
    0x00cf92000000ffff,  // 0x10 カーネルのデータ
    0x00cff2000000ffff,  // 0x18 ユーザーのデータ（特権レベル 3）
    0x00affa000000ffff,  // 0x20 ユーザーのコード（64 ビット、特権レベル 3）
    0, 0,                // 0x28 TSS（16 バイト）
};

struct tss {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3, iomap;
} __attribute__((packed));

static struct tss tss;

struct idt_entry {
    u16 offset_lo, selector;
    u8 ist, flags;
    u16 offset_mid;
    u32 offset_hi, reserved;
} __attribute__((packed));

static struct idt_entry idt[48];

extern u64 isr_table[48];

void cpu_set_kernel_stack(u64 top) { tss.rsp0 = top; }

void cpu_init(void) {
    u64 base = (u64)&tss, limit = sizeof tss - 1;
    gdt[5] = (limit & 0xffff) | ((base & 0xffffff) << 16) | (0x89UL << 40) | (((limit >> 16) & 0xf) << 48) |
             (((base >> 24) & 0xff) << 56);
    gdt[6] = base >> 32;
    tss.iomap = sizeof tss;
    struct { u16 limit; u64 base; } __attribute__((packed)) gp = {sizeof gdt - 1, (u64)gdt};
    __asm__ volatile("lgdt %0" ::"m"(gp));
    __asm__ volatile("ltr %w0" ::"r"(0x28));

    for (int i = 0; i < 48; i++) {
        u64 h = isr_table[i];
        idt[i] = (struct idt_entry){(u16)h, 0x08, 0, 0x8e, (u16)(h >> 16), (u32)(h >> 32), 0};
    }
    struct { u16 limit; u64 base; } __attribute__((packed)) ip = {sizeof idt - 1, (u64)idt};
    __asm__ volatile("lidt %0" ::"m"(ip));
}
