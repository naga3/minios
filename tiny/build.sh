#!/bin/sh
# ../build/tiny/kernel.elf を作る（QEMU の -kernel で起動する）。OS はアセンブリ、ゲームは C
set -e
# C（ゲーム）: ライブラリなし。割り込みがスタックの下を使うので red zone を使わない。浮動小数点・SSE も使わない（CPU で有効にしていない）
CFLAGS="-O2 -std=c11 -Wall -Wextra -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -mno-red-zone -mgeneral-regs-only"
cd "$(dirname "$0")"
OUT=../build/tiny
mkdir -p $OUT
objs=""
for src in boot.s irq.s screen.s font.s console.s kbd.s ac97.s c.s sprite.s snake.s main.s; do
    as --64 -o $OUT/${src%.s}.o $src
    objs="$objs $OUT/${src%.s}.o"
done
for src in sound.c apu.c mml.c robo.c actors.c stages.c pics.c; do
    gcc $CFLAGS -c -o $OUT/${src%.c}.o $src
    objs="$objs $OUT/${src%.c}.o"
done
ld -nostdlib -static -z max-page-size=0x1000 --no-warn-rwx-segments -T linker.ld -o $OUT/kernel64.elf $objs
objcopy -O elf32-i386 $OUT/kernel64.elf $OUT/kernel.elf  # QEMU の Multiboot は 32 ビットの ELF しか読まないので、形式だけ変える
