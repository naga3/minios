#!/bin/sh
# 自作カーネルをビルドして ../build/scratch/kernel.elf に置く（QEMU の -kernel で起動する Multiboot の ELF）
set -e
cd "$(dirname "$0")"
OUT=../build/scratch
mkdir -p $OUT
CFLAGS="-ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mgeneral-regs-only -mcmodel=small -O2 -Wall -Werror -nostdlib"
objs=""
for src in *.S *.c; do
    obj=$OUT/$(basename "$src").o
    gcc $CFLAGS -c -o "$obj" "$src"
    objs="$objs $obj"
done
ld -nostdlib -static -z max-page-size=0x1000 --no-warn-rwx-segments -T linker.ld -o $OUT/kernel64.elf $objs
# QEMU の Multiboot の読み込みは 32 ビットの ELF しか受け付けないので、形式だけ変える（中身は 64 ビットのまま）
objcopy -O elf32-i386 $OUT/kernel64.elf $OUT/kernel.elf
