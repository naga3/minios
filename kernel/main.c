// minios の自作カーネル
#include "kernel.h"

// Multiboot の情報（使う所だけ）
struct multiboot_info {
    u32 flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr;
};

struct multiboot_module {
    u32 start, end, string, reserved;
};

extern char kernel_end[];

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

    // 空いている物理メモリ: カーネルと initramfs（Multiboot のモジュール）の後ろから、メモリの終わりまで
    // ファイルシステムを作るのにメモリの割り当てを使うので、先に mm_init する
    u64 free_start = (u64)kernel_end;
    struct multiboot_module *mod = 0;
    if ((mb->flags & 8) && mb->mods_count > 0) {
        mod = (struct multiboot_module *)(u64)mb->mods_addr;
        if (mod->end > free_start) free_start = mod->end;
    }
    mm_init(free_start, (u64)(mb->mem_upper + 1024) * 1024);
    if (mod) {
        vfs_init(P2V(mod->start), mod->end - mod->start);
        kprint("initramfs: "); kprint_u((mod->end - mod->start) / 1024); kprint(" KiB\n");
    } else {
        vfs_init(0, 0);
    }

    cpu_init();
    irq_init();
    syscall_init();
    __asm__ volatile("sti");
    kprint("interrupts on\n");

    // 起動オプションで例外を起こして、例外の処理を確かめる（テスト用）
    if (str_eq(cmdline, "test=ud2")) __asm__ volatile("ud2");
    if (str_eq(cmdline, "test=pagefault")) *(volatile u64 *)0x8000000000UL = 1;

    // 起動オプション "run=パス 引数..." があれば、initramfs のそのプログラムを最初のプロセスとして動かす
    if (cmdline[0] == 'r' && cmdline[1] == 'u' && cmdline[2] == 'n' && cmdline[3] == '=') {
        static char buf[256];
        static char *argv[16];
        int argc = 0, n = 0;
        for (const char *p = cmdline + 4; *p && n < 255; p++) buf[n++] = *p;
        buf[n] = 0;
        for (char *p = buf; *p && argc < 15;) {
            argv[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = 0;
        }
        argv[argc] = 0;
        static char *envp[] = {"HOME=/", "TERM=linux", 0};  // Linux がカーネルから init に渡すのと同じ
        extern int serial_to_tty;
        serial_to_tty = 1;
        kprint("exec "); kprint(argv[0]); kprint("\n");
        long e = exec_initial(argv[0], argv, envp);
        kprint("exec failed: "); kprint_u(-e); kprint("\n");
        qemu_exit(1);
    }

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
