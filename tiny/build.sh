#!/bin/sh
# ../build/tiny/kernel.elf を作る（QEMU の -kernel で起動する）。使うのはアセンブラとリンカ（binutils）だけ
set -e
cd "$(dirname "$0")"
OUT=../build/tiny
mkdir -p $OUT
as --64 -o $OUT/boot.o boot.s
as --64 -o $OUT/main.o main.s
ld -nostdlib -static -z max-page-size=0x1000 --no-warn-rwx-segments -T linker.ld -o $OUT/kernel64.elf $OUT/boot.o $OUT/main.o
objcopy -O elf32-i386 $OUT/kernel64.elf $OUT/kernel.elf  # QEMU の Multiboot は 32 ビットの ELF しか読まないので、形式だけ変える
