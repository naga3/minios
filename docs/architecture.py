#!/usr/bin/env python3
"""minios の自作カーネル（kernel/）の全体図を docs/architecture.png に描く。python3 docs/architecture.py"""
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 1900, 1250
FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
BOLD = "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc"
f = lambda size, bold=False: ImageFont.truetype(BOLD if bold else FONT, size, index=0)  # index 0 は JP

# 色（塗り、線）
ASM = ("#fde9d4", "#d9822b")     # アセンブリ
C = ("#dbe8f7", "#3b73b9")       # C
USER = ("#dff2e1", "#3a8f4a")    # ユーザーのプログラム
HW = ("#ececec", "#7a7a7a")      # ハードウェア（QEMU）
INK, SUB = "#222222", "#555555"

img = Image.new("RGB", (W, H), "white")
d = ImageDraw.Draw(img)


def box(x, y, w, h, colors, title, lines=(), size=17, radius=10):
    fill, line = colors
    d.rounded_rectangle((x, y, x + w, y + h), radius, fill=fill, outline=line, width=2)
    d.text((x + w / 2, y + 10), title, font=f(size, True), fill=INK, anchor="ma")
    for i, t in enumerate(lines):
        d.text((x + w / 2, y + 16 + size + i * (size - 1)), t, font=f(size - 4), fill=SUB, anchor="ma")


def band(x, y, w, h, fill, line, label):
    d.rounded_rectangle((x, y, x + w, y + h), 14, fill=fill, outline=line, width=2)
    d.text((x + 14, y + 8), label, font=f(18, True), fill=line)


def arrow(x1, y1, x2, y2, color=INK, width=3, head=12):
    d.line((x1, y1, x2, y2), fill=color, width=width)
    import math
    a = math.atan2(y2 - y1, x2 - x1)
    for s in (+1, -1):
        d.line((x2, y2, x2 - head * math.cos(a + s * 0.45), y2 - head * math.sin(a + s * 0.45)), fill=color, width=width)


# ---- 題 ----
d.text((30, 18), "minios 自作カーネルの全体図", font=f(34, True), fill=INK)
d.text((30, 66), "C とアセンブリで 2,517 行、46KB。Linux と同じシステムコールを用意して、Linux 用にビルドしたプログラムを変えずに動かす",
       font=f(19), fill=SUB)

# ---- 左: 起動の流れ ----
X0 = 30
d.text((X0, 115), "起動の流れ", font=f(20, True), fill=INK)
steps = [
    (HW, "QEMU（Multiboot）", ["kernel.elf と initramfs を", "メモリに置いて 32 ビットで呼ぶ"]),
    (ASM, "boot.S", ["ページテーブルを作り", "64 ビットに切り替える"]),
    (C, "kmain（main.c）", ["メモリ・ファイルシステム・", "割り込み・syscall の準備"]),
    (C, "/init を exec", ["最初のプロセス（PID 1）", "としてユーザーモードへ"]),
    (USER, "init → シェル", ["マウント、端末ごとに", "シェルを起動して見守る"]),
]
y = 150
for i, (col, t, ls) in enumerate(steps):
    box(X0, y, 280, 96, col, t, ls, size=18)
    if i < len(steps) - 1:
        arrow(X0 + 140, y + 96, X0 + 140, y + 124)
    y += 124

# ---- 中央: 層 ----
CX, CW = 350, 1080
# ユーザーモード
band(CX, 110, CW, 190, "#f4fbf5", USER[1], "ユーザーモード（リング 3）── Linux 用にビルドしたプログラムをそのまま動かす")
progs = [("/init", "PID 1"), ("sh", "端末ごとのシェル"), ("hello・grep", "小さなプログラム"), ("forktest・fstest", "動作確認用")]
for i, (t, s) in enumerate(progs):
    box(CX + 25 + i * 262, 155, 240, 70, USER, t, [s], size=18)
d.text((CX + 25, 240), "番地は Linux と同じ（プログラムは 0x400000 から、スタックは 0x40000000 から下へ）。プロセスごとに自分のページテーブルを持つ",
       font=f(15), fill=SUB)
d.text((CX + 25, 263), "fork・execve・wait4・read・write・open・pipe・dup2・ioctl など、Linux と同じ番号で呼ぶ（約 40 個）",
       font=f(15), fill=SUB)

# 境界の矢印
arrow(CX + 170, 300, CX + 170, 370, USER[1], 4)
d.text((CX + 182, 318), "syscall 命令（rax = 番号）", font=f(16, True), fill=USER[1])
arrow(CX + 470, 370, CX + 470, 300, USER[1], 4)
d.text((CX + 482, 318), "sysretq・iretq で戻る", font=f(16, True), fill=USER[1])
arrow(CX + 830, 300, CX + 830, 370, "#b03030", 4)
d.text((CX + 842, 318), "例外（ゼロ除算など）", font=f(16, True), fill="#b03030")

# カーネル
band(CX, 370, CW, 560, "#f6f9fd", C[1], "カーネル（リング 0）")
# 入り口（アセンブリ）
box(CX + 25, 410, 330, 80, ASM, "syscall.S（入り口と出口）", ["ユーザーのスタックからカーネルのスタックへ", "レジスタを全部積んで C を呼ぶ"])
box(CX + 725, 410, 330, 80, ASM, "isr.S（割り込みの入り口）", ["例外と割り込み（タイマー・キー入力）", "レジスタを全部積んで C を呼ぶ"])
# 振り分け
box(CX + 25, 520, 330, 80, C, "syscall_dispatch（syscall.c）", ["rax の番号で振り分ける", "0=read 1=write 57=fork 59=execve …"])
box(CX + 725, 520, 330, 80, C, "interrupt（irq.c）", ["タイマー: 50ms ごとに別のプロセスへ", "ユーザーの例外: そのプロセスを終わらせる"])
arrow(CX + 190, 490, CX + 190, 520)
arrow(CX + 890, 490, CX + 890, 520)
# 下の部品
parts = [
    (C, "proc.c", ["プロセスの表・スケジューラ", "fork・execve・exit・wait4"]),
    (ASM, "swtch.S", ["カーネルスタックを", "取り替えて切り替える"]),
    (C, "vfs.c・file.c", ["メモリ上のファイルシステム", "ファイル記述子"]),
    (C, "tty.c・pipe.c", ["端末（1 行ずつ・エコー・Ctrl+C）", "パイプ（4KB の輪）"]),
]
for i, (col, t, ls) in enumerate(parts):
    box(CX + 25 + i * 262, 640, 240, 88, col, t, ls, size=18)
parts2 = [
    (C, "mm.c・kmalloc.c", ["4KB のページ・ページテーブル", "カーネルの小さな割り当て"]),
    (C, "signal.c", ["SIGINT などで終わらせる", "無視（SIG_IGN）だけ扱う"]),
    (C, "cpu.c", ["GDT・TSS・IDT", "SSE を有効にする"]),
    (C, "serial.c・sys_misc.c", ["シリアルの入出力", "時計・uname・mount・reboot"]),
]
for i, (col, t, ls) in enumerate(parts2):
    box(CX + 25 + i * 262, 750, 240, 88, col, t, ls, size=18)
# 振り分けから各部品へ: 横の通り道（y=620）から下ろす
BUS = 620
d.line((CX + 190, 600, CX + 190, BUS), fill=INK, width=3)
d.line((CX + 890, 600, CX + 890, BUS), fill=INK, width=3)
d.line((CX + 145, BUS, CX + 931, BUS), fill=INK, width=3)
for cx in (CX + 145, CX + 669, CX + 931):
    arrow(cx, BUS, cx, 640)
arrow(CX + 265, 684, CX + 287, 684, ASM[1], 3)  # proc → swtch
d.text((CX + 300, BUS - 22), "それぞれの部品を呼ぶ", font=f(14), fill=SUB)
d.text((CX + 25, 855), "カーネルは SSE を使わないようにビルドし、プロセスを切り替えるときにユーザーの SSE のレジスタを保存・復元する（fxsave・fxrstor）",
       font=f(15), fill=SUB)
d.text((CX + 25, 878), "物理メモリは全部、高い番地（0xFFFF800000000000〜）に写して触る。ファイルの中身は initramfs を指したまま使い、書き換えるときにコピーする",
       font=f(15), fill=SUB)

# ハードウェア
band(CX, 950, CW, 150, "#f7f7f7", HW[1], "ハードウェア（QEMU）")
hw = [("CPU", "x86-64"), ("メモリ", "256MB"), ("シリアル", "COM1（端末）"), ("タイマー", "PIT 100Hz・PIC"),
      ("CMOS の時計", "起動時の時刻"), ("終了装置", "isa-debug-exit")]
for i, (t, s) in enumerate(hw):
    box(CX + 25 + i * 173, 995, 158, 72, HW, t, [s], size=18)
# シリアル（キー入力）とタイマーの割り込み: 右端の通り道を上がって isr.S へ
RAIL = CX + 1068
for bx in (CX + 25 + 2 * 173 + 79, CX + 25 + 3 * 173 + 79):  # シリアル、タイマーの箱の中央
    d.line((bx, 995, bx, 975), fill="#b06a1a", width=3)
d.line((CX + 25 + 2 * 173 + 79, 975, RAIL, 975), fill="#b06a1a", width=3)
d.line((RAIL, 975, RAIL, 450), fill="#b06a1a", width=3)
arrow(RAIL, 450, CX + 1057, 450, "#b06a1a", 3)
d.text((RAIL - 8, 969), "割り込み（キー入力・タイマー）", font=f(14, True), fill="#b06a1a", anchor="rs")

# ---- 右: メモリの地図 ----
RX = 1460
d.text((RX, 115), "1 つのプロセスから見た番地", font=f(20, True), fill=INK)
mem = [
    ("#dbe8f7", "0xFFFF800000000000〜", "物理メモリ全体（カーネル専用）", "全プロセスで共通", 120),
    ("#ffffff", "", "（使わない）", "", 60),
    ("#dff2e1", "〜 0x40000000", "ユーザーのスタック（下へ伸びる）", "argc・argv・envp を積む", 90),
    ("#ffffff", "", "（空き）", "", 60),
    ("#dff2e1", "0x400000〜", "プログラム（ELF）", "コード・データ", 90),
    ("#fde9d4", "0〜2MB", "カーネルのコードとデータ", "カーネル専用", 80),
]
y = 150
for fill, addr, t, s, h in mem:
    d.rectangle((RX, y, RX + 400, y + h), fill=fill, outline="#999999", width=2)
    d.text((RX + 12, y + 8), t, font=f(17, True), fill=INK)
    if s:
        d.text((RX + 12, y + 34), s, font=f(15), fill=SUB)
    if addr:
        d.text((RX + 390, y + h - 8), addr, font=f(14), fill=SUB, anchor="rs")
    y += h

d.text((RX, 680), "物理メモリ（256MB）", font=f(20, True), fill=INK)
phys = [("#fde9d4", "1MB〜: カーネル（46KB と作業用の領域）"), ("#ececec", "その後: initramfs（cpio のまま）"),
        ("#dbe8f7", "残り全部: 空きページ（4KB ずつ配る）")]
y = 715
for fill, t in phys:
    d.rectangle((RX, y, RX + 400, y + 56), fill=fill, outline="#999999", width=2)
    d.text((RX + 12, y + 16), t, font=f(15), fill=INK)
    y += 56

# ---- 凡例 ----
y = 1135
d.text((30, y + 4), "凡例", font=f(18, True), fill=INK)
for i, (col, t) in enumerate([(ASM, "アセンブリ（C では書けない所: モードの切り替え、レジスタの全保存、スタックの取り替え）"),
                               (C, "C"), (USER, "ユーザーのプログラム（Linux 版と共通）"), (HW, "ハードウェア（QEMU）")]):
    x = 100 + [0, 760, 880, 1300][i]
    d.rounded_rectangle((x, y + 4, x + 30, y + 28), 5, fill=col[0], outline=col[1], width=2)
    d.text((x + 40, y + 4), t, font=f(16), fill=INK)
d.text((30, y + 50), "流れの例: シェルが hello を動かす → fork（proc.c がメモリをコピー）→ execve（ELF を読み込み、スタックに引数を積む）→ hello が write → "
       "syscall.S → sys_write → tty → シリアル → 画面", font=f(16), fill=SUB)

out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "architecture.png")
img.save(out)
print(out)
