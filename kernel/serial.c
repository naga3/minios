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
    outb(COM1 + 4, 0x0B);  // 割り込みを PIC に届ける（OUT2）
    outb(COM1 + 1, 0x01);  // 受信したら割り込む
}

// 受信した文字の輪（割り込みで入れ、serial_getc で取り出す）
static char rx[256];
static volatile unsigned rx_head, rx_tail;

// 受信した文字は、プロセスが動いていれば端末（tty）へ、まだなら輪（起動直後のキー入力の確認用）へ
int serial_to_tty;

void serial_irq(void) {
    while (inb(COM1 + 5) & 1) {
        char c = inb(COM1);
        if (serial_to_tty) tty_input(c);
        else if (rx_head - rx_tail < sizeof rx) rx[rx_head++ % sizeof rx] = c;
    }
}

int serial_getc(void) {
    if (rx_tail == rx_head) return -1;
    return (unsigned char)rx[rx_tail++ % sizeof rx];
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

void kprint_u(u64 v) {
    char b[21];
    int i = sizeof b;
    b[--i] = 0;
    do b[--i] = '0' + v % 10; while (v /= 10);
    kprint(b + i);
}

void kprint_hex(u64 v) {
    char b[19];
    b[0] = '0', b[1] = 'x', b[18] = 0;
    for (int i = 17; i >= 2; i--, v >>= 4) b[i] = "0123456789abcdef"[v & 15];
    kprint(b);
}
