#!/usr/bin/env python3
"""tiny/（いちばん小さい自作カーネル）の全体図を docs/tiny.png に描く。python3 docs/tiny.py"""
import math, os
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
BOLD = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"
f = lambda size, bold=False: ImageFont.truetype(BOLD if bold else FONT, size, index=0)
ASM, C, HW = ("#fde9d4", "#d9822b"), ("#dbe8f7", "#3b73b9"), ("#ececec", "#7a7a7a")
INK, SUB = "#222222", "#555555"

W, H = 1400, 760
img = Image.new("RGB", (W, H), "white")
d = ImageDraw.Draw(img)


def box(x, y, w, h, colors, title, lines=()):
    d.rounded_rectangle((x, y, x + w, y + h), 12, fill=colors[0], outline=colors[1], width=2)
    d.text((x + w / 2, y + 14), title, font=f(22, True), fill=INK, anchor="ma")
    for i, t in enumerate(lines):
        d.text((x + w / 2, y + 50 + i * 24), t, font=f(17), fill=SUB, anchor="ma")


def arrow(x1, y1, x2, y2, color=INK, label=None, width=3):
    d.line((x1, y1, x2, y2), fill=color, width=width)
    a = math.atan2(y2 - y1, x2 - x1)
    for s in (+1, -1):
        d.line((x2, y2, x2 - 14 * math.cos(a + s * 0.45), y2 - 14 * math.sin(a + s * 0.45)), fill=color, width=width)
    if label:
        d.text(((x1 + x2) / 2 + 10, (y1 + y2) / 2 - 12), label, font=f(16, True), fill=color)


d.text((40, 24), "minios tiny の全体図（echo の版。今は Forth。forth.s を参照）", font=f(34, True), fill=INK)
d.text((40, 72), "アセンブリだけで 169 行、5.4KB（C もコンパイラも使わない）。割り込みもユーザーモードも使わず、シリアルを見張って 1 行ずつ返す", font=f(19), fill=SUB)

box(40, 150, 300, 150, HW, "QEMU（Multiboot）", ["kernel.elf をメモリに置いて", "32 ビットのまま _start を呼ぶ"])
box(420, 150, 360, 150, ASM, "boot.s（57 行）", ["最初の 1GB を写すページテーブル", "PAE・ロングモード・ページング", "→ 64 ビットに入って main を呼ぶ"])
box(860, 150, 500, 150, ASM, "main（main.s、112 行）", ["「> 」を出す → 1 行読む → そのまま返す", "をずっと繰り返す"])
arrow(340, 225, 420, 225)
arrow(780, 225, 860, 225)

box(860, 380, 240, 150, ASM, "read_line", ["1 文字ずつ読んで溜める", "その場で表示（エコー）", "バックスペースで消す"])
box(1120, 380, 240, 150, ASM, "puts・putc", ["1 文字ずつ送る", "改行の前に \\r を足す"])
arrow(980, 300, 980, 380)
arrow(1240, 300, 1240, 380)

box(860, 600, 500, 120, HW, "シリアルポート（COM1、I/O ポート 0x3f8）", ["届いたか・送れるかを見張る（0x3fd の状態を読む）", "inb で 1 文字読み、outb で 1 文字送る"])
arrow(980, 530, 980, 600, "#b06a1a", "getc: inb")
arrow(1240, 530, 1240, 600, "#b06a1a", "putc: outb")

d.text((40, 390), "動き", font=f(22, True), fill=INK)
flow = ["1. QEMU が kernel.elf を 1MB の番地に置く", "2. boot.s がページテーブルを作り 64 ビットへ", "3. main が「> 」を出す",
        "4. キーを打つと、シリアルに文字が届く", "5. getc が届いた文字を読み、putc で表示", "6. Enter で、溜めた行をそのまま返す",
        "7. 3 に戻る"]
for i, t in enumerate(flow):
    d.text((40, 432 + i * 34), t, font=f(18), fill=INK)
d.text((40, 690), "無いもの: 割り込み、プロセス、ユーザーモード、ファイル、メモリの割り当て", font=f(17), fill=SUB)

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tiny.png")
img.save(out)
print(out)
