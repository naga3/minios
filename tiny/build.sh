#!/bin/sh
# ../build/tiny/kernel.elf を作る（QEMU の -kernel で起動する）。使うのはアセンブラとリンカ（binutils）だけ
set -e
cd "$(dirname "$0")"
OUT=../build/tiny
mkdir -p $OUT
objs=""
for src in boot.s irq.s screen.s font.s console.s kbd.s sprite.s snake.s jump.s main.s; do
    as --64 -o $OUT/${src%.s}.o $src
    objs="$objs $OUT/${src%.s}.o"
done
ld -nostdlib -static -z max-page-size=0x1000 --no-warn-rwx-segments -T linker.ld -o $OUT/kernel64.elf $objs
objcopy -O elf32-i386 $OUT/kernel64.elf $OUT/kernel.elf  # QEMU の Multiboot は 32 ビットの ELF しか読まないので、形式だけ変える
