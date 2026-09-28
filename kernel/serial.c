// シリアルポート（COM1、I/O ポート 0x3f8、16550 互換）
#include "kernel.h"

#define COM1 0x3f8

void serial_init(void) {
    outb(COM1 + 1, 0x00);  // 割り込みを止める
    outb(COM1 + 3, 0x80);  // 速度の設定を始める
    outb(COM1 + 0, 0x01);  // 115200 bps
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);  // 8 ビット、パリティなし、ストップビット 1
    outb(COM1 + 2, 0xC7);  // FIFO
}

void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20)) {}  // 送れるようになるまで待つ
    outb(COM1, c);
}

void kprint(const char *s) {
    for (; *s; s++) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s);
    }
}
