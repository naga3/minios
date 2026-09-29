#!/bin/sh
# ビルドして QEMU で起動する。Ctrl+C はゲストに届く。QEMU を強制終了するには Ctrl+A → X
# build/models.img（./build-models.sh で作る）があれば、モデルのディスクをつなぎ、ローカル LLM（grow）が動くメモリと CPU にする
set -e
cd "$(dirname "$0")"
# ./run.sh tiny: アセンブリだけの自作カーネル（tiny/。320 × 200・256 色の画面と PS/2 キーボード）で起動する。
# 画面はウィンドウに出る（WSL なら WSLg）。ウィンドウを閉じると終わる。
# Ctrl+Alt+F でフルスクリーンの切り替え、Ctrl+Alt+G でウィンドウに取られたマウスとキーボードを返してもらう
# 音は Sound Blaster 16 から、PulseAudio（WSL なら WSLg）へ。PulseAudio が無ければ鳴らさない
# 画面は SDL で出す。GTK だと、Sound Blaster の DMA が動き続けている間 QEMU のメインスレッドが 100% に張り付き、
# ウィンドウが描き直されなくなる（起動時の BIOS の文字のまま止まって見える）
if [ "$1" = tiny ]; then
    tiny/build.sh
    audio="none,id=snd"
    if [ -n "$PULSE_SERVER" ] || [ -S "${XDG_RUNTIME_DIR:-/nonexistent}/pulse/native" ]; then audio="pa,id=snd"; fi
    accel="-accel tcg"
    [ -r /dev/kvm ] && [ -w /dev/kvm ] && accel="-accel kvm -cpu host"
    exec qemu-system-x86_64 $accel -kernel build/tiny/kernel.elf -display sdl -serial none -audiodev $audio -device sb16,audiodev=snd
fi
# ./run.sh scratch: Linux の代わりに自作カーネル（kernel/）で起動する
if [ "$1" = scratch ]; then
    ./build.sh          # initramfs（/init やシェル、プログラム）は Linux 版と同じものを使う
    kernel/build.sh
    accel="-accel tcg"
    [ -r /dev/kvm ] && [ -w /dev/kvm ] && accel="-accel kvm -cpu host"
    exec qemu-system-x86_64 $accel -kernel build/scratch/kernel.elf -initrd build/initramfs.cpio -m 256M \
        -device isa-debug-exit,iobase=0xf4,iosize=0x04 \
        -display none -serial mon:stdio -no-reboot
fi
./build.sh
# KVM が使えれば使う。使えなければソフトウェアエミュレーション
accel="-accel tcg"
[ -r /dev/kvm ] && [ -w /dev/kvm ] && accel="-accel kvm -cpu host"
models="-m 256M"
[ -f build/models.img ] && models="-m 8G -smp 8 -drive file=build/models.img,format=raw,if=virtio,readonly=on"
exec qemu-system-x86_64 $accel \
    -kernel build/bzImage \
    -initrd build/initramfs.cpio \
    -append "console=ttyS0 loglevel=0" \
    -drive file=disk.img,format=raw,if=virtio \
    $models \
    -nic user,model=virtio-net-pci \
    -display none -serial mon:stdio -no-reboot
