// minios の自作カーネル
#include "kernel.h"

void kmain(unsigned int magic, unsigned int mbinfo) {
    (void)mbinfo;
    serial_init();
    kprint("\nminios scratch kernel\n");
    if (magic != 0x2BADB002) kprint("warning: not started by a Multiboot loader\n");
    kprint("hello from 64-bit long mode\n");
    qemu_exit(0);
}
