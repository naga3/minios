// 割り込み: 例外、PIC（8259）、タイマー（PIT、100Hz）
#include "kernel.h"

static const char *const exception_names[32] = {
    "divide error", "debug", "NMI", "breakpoint", "overflow", "bound range", "invalid opcode", "device not available",
    "double fault", "coprocessor", "invalid TSS", "segment not present", "stack fault", "general protection",
    "page fault", "reserved", "x87 floating point", "alignment check", "machine check", "SIMD floating point",
};

volatile u64 ticks;

static void pic_init(void) {
    // 2 つの PIC の割り込み番号を 32〜47 に移す（0〜31 は CPU の例外と重なるので）
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 32);   outb(0xA1, 40);
    outb(0x21, 4);    outb(0xA1, 2);
    outb(0x21, 1);    outb(0xA1, 1);
    // 使う割り込みだけ通す: IRQ0（タイマー）、IRQ2（2 つ目の PIC へのつなぎ）、IRQ4（COM1）
    outb(0x21, ~(1 << 0 | 1 << 2 | 1 << 4) & 0xff);
    outb(0xA1, 0xff);
}

static void pit_init(void) {
    u16 div = 1193182 / 100;
    outb(0x43, 0x36);
    outb(0x40, div & 0xff);
    outb(0x40, div >> 8);
}

void irq_init(void) {
    pic_init();
    pit_init();
}

void interrupt(struct regs *r) {
    if (r->vector < 32) {
        kprint("\n*** exception: ");
        kprint(exception_names[r->vector] ? exception_names[r->vector] : "?");
        kprint(" (vector "); kprint_u(r->vector);
        kprint(", error "); kprint_hex(r->error);
        kprint(", rip "); kprint_hex(r->rip);
        if (r->vector == 14) {
            u64 cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            kprint(", address "); kprint_hex(cr2);
        }
        kprint(")\n");
        qemu_exit(3);
    }
    u64 irq = r->vector - 32;
    if (irq >= 8) outb(0xA0, 0x20);  // 割り込みの終わりを PIC に知らせる（切り替えの前に）
    outb(0x20, 0x20);
    if (irq == 4) serial_irq();
    if (irq == 0) {
        ticks++;
        // ユーザーモードで動いていたプロセスを、50ms ごとに横取りして他に回す（カーネルの中では横取りしない）
        if ((r->cs & 3) == 3 && ticks % 5 == 0) schedule();
    }
}
