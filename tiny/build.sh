#!/bin/sh
# ../build/tiny/kernel.elf を作る（QEMU の -kernel で起動する）
set -e
cd "$(dirname "$0")"
OUT=../build/tiny
mkdir -p $OUT
CFLAGS="-ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mgeneral-regs-only -O2 -Wall -Werror -fno-builtin"
gcc $CFLAGS -c -o $OUT/boot.o boot.S
gcc $CFLAGS -c -o $OUT/main.o main.c
ld -nostdlib -static -z max-page-size=0x1000 --no-warn-rwx-segments -T linker.ld -o $OUT/kernel64.elf $OUT/boot.o $OUT/main.o
objcopy -O elf32-i386 $OUT/kernel64.elf $OUT/kernel.elf  # QEMU の Multiboot は 32 ビットの ELF しか読まないので、形式だけ変える
