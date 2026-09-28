// minios tiny: シリアルから 1 行読んで、そのまま返す
typedef unsigned char u8;
typedef unsigned short u16;

static void outb(u16 port, u8 v) { __asm__ volatile("outb %0, %1" ::"a"(v), "Nd"(port)); }
static u8 inb(u16 port) {
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

#define COM1 0x3f8

static void putc(char c) {
    while (!(inb(COM1 + 5) & 0x20)) {}  // 送れるまで待つ
    outb(COM1, c);
}

static char getc(void) {
    while (!(inb(COM1 + 5) & 0x01)) {}  // 文字が届くまで待つ
    return inb(COM1);
}

static void puts(const char *s) {
    for (; *s; s++) {
        if (*s == '\n') putc('\r');
        putc(*s);
    }
}

// 1 行読む。打った文字はその場で表示し、バックスペースで消せる
static void read_line(char *buf, int size) {
    int n = 0;
    for (;;) {
        char c = getc();
        if (c == '\r' || c == '\n') break;
        if (c == 0x7f || c == '\b') {
            if (n > 0) {
                n--;
                puts("\b \b");
            }
            continue;
        }
        if (n < size - 1) {
            buf[n++] = c;
            putc(c);
        }
    }
    buf[n] = 0;
    puts("\n");
}

void main(void) {
    puts("\nminios tiny\n");
    for (;;) {
        char line[256];
        puts("> ");
        read_line(line, sizeof line);
        puts(line);
        puts("\n");
    }
}
