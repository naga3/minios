#!/bin/sh
# ビルドして QEMU で起動する。Ctrl+C はゲストに届く。QEMU を強制終了するには Ctrl+A → X
# build/models.img（./build-models.sh で作る）があれば、モデルのディスクをつなぎ、ローカル LLM（grow）が動くメモリと CPU にする
set -e
cd "$(dirname "$0")"
./build.sh
# KVM が使えれば使う。使えなければソフトウェアエミュレーション
accel="-accel tcg"
[ -r /dev/kvm ] && [ -w /dev/kvm ] && accel="-accel kvm -cpu host"
models="-m 256M"
[ -f build/models.img ] && models="-m 3G -smp 8 -drive file=build/models.img,format=raw,if=virtio,readonly=on"
exec qemu-system-x86_64 $accel \
    -kernel build/bzImage \
    -initrd build/initramfs.cpio \
    -append "console=ttyS0 loglevel=0" \
    -drive file=disk.img,format=raw,if=virtio \
    $models \
    -nic user,model=virtio-net-pci \
    -display none -serial mon:stdio -no-reboot
