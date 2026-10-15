#!/bin/sh
# ../build/pi/kernel8.img を作る（Pi 3 / QEMU の raspi3b 用。aarch64）。
# ゲーム（robo・actors・stages・pics）と音源（sound・apu・mml）は ../tiny のものを -DHOST でそのまま使う。
# OS の関数（画面・文字・キー・時計）は hal.c が Pi 向けに実装する
set -e
cd "$(dirname "$0")"
OUT=../build/pi
T=../tiny
mkdir -p $OUT
# フォントは tiny/font.s の点の絵をそのまま C の表にする（0x20〜0x7e の 95 文字 × 8 バイト）
awk 'BEGIN { print "// font.s から build.sh が作る。直接は直さない\nconst unsigned char font[95 * 8] = {" }
     /^[ \t]*\.byte 0b/ { v = $2; sub(/^0b/, "", v); n = 0; for (i = 1; i <= 8; i++) n = n * 2 + substr(v, i, 1); printf "%d,", n; if (++k % 8 == 0) print "" }
     END { print "};" }' $T/font.s > $OUT/font.h
CC=aarch64-linux-gnu-gcc
CFLAGS="-O2 -std=gnu11 -Wall -Wextra -ffreestanding -fno-builtin -nostdlib -mgeneral-regs-only -mstrict-align -fno-tree-loop-distribute-patterns -DHOST -I$OUT -I$T -I."
$CC $CFLAGS -c start.S -o $OUT/start.o
objs="$OUT/start.o"
for src in hal.c main.c $T/sound.c $T/apu.c $T/mml.c $T/robo.c $T/actors.c $T/stages.c $T/pics.c; do
    o=$OUT/$(basename ${src%.c}).o
    $CC $CFLAGS -c $src -o $o
    objs="$objs $o"
done
aarch64-linux-gnu-ld -nostdlib --no-warn-rwx-segments -T linker.ld -o $OUT/kernel8.elf $objs
aarch64-linux-gnu-objcopy -O binary $OUT/kernel8.elf $OUT/kernel8.img
ls -l $OUT/kernel8.img
