// minios の自作カーネル
#include "kernel.h"

// Multiboot の情報（使う所だけ）
struct multiboot_info {
    u32 flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr;
};

static int str_eq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

void kmain(unsigned int magic, unsigned int mbinfo) {
    serial_init();
    kprint("\nminios scratch kernel\n");
    if (magic != 0x2BADB002) kprint("warning: not started by a Multiboot loader\n");
    struct multiboot_info *mb = (struct multiboot_info *)(u64)mbinfo;
    // QEMU は起動オプションの先頭にカーネルのファイルのパスを付けて渡すので、最初の単語を飛ばす
    const char *cmdline = (mb->flags & 4) ? (const char *)(u64)mb->cmdline : "";
    while (*cmdline && *cmdline != ' ') cmdline++;
    while (*cmdline == ' ') cmdline++;
    kprint("memory: "); kprint_u((mb->mem_upper + 1024) / 1024); kprint(" MiB\n");

    cpu_init();
    irq_init();
    __asm__ volatile("sti");
    kprint("interrupts on\n");

    // 起動オプションで例外を起こして、例外の処理を確かめる（テスト用）
    if (str_eq(cmdline, "test=ud2")) __asm__ volatile("ud2");
    if (str_eq(cmdline, "test=pagefault")) *(volatile u64 *)0x8000000000UL = 1;

    kprint("type keys (q to quit)\n");
    u64 last = 0;
    for (;;) {
        int c;
        while ((c = serial_getc()) >= 0) {
            if (c == 'q') {
                kprint("\nuptime "); kprint_u(ticks / 100); kprint("."); kprint_u(ticks % 100 / 10); kprint("s, bye\n");
                qemu_exit(0);
            }
            if (c == '\r') c = '\n';
            serial_putc(c);
        }
        if (ticks / 100 != last / 100 && ticks % 100 == 0) last = ticks;  // 1 秒ごとの印（今は数えるだけ）
        __asm__ volatile("hlt");  // 次の割り込みまで眠る
    }
}
