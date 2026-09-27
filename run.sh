#!/bin/sh
# ビルドして QEMU で起動する。Ctrl+C はゲストに届く。QEMU を強制終了するには Ctrl+A → X
set -e
cd "$(dirname "$0")"
./build.sh
# KVM が使えれば使う。使えなければソフトウェアエミュレーション
accel="-accel tcg"
[ -r /dev/kvm ] && [ -w /dev/kvm ] && accel="-accel kvm -cpu host"
exec qemu-system-x86_64 $accel -m 64M \
    -kernel build/bzImage \
    -initrd build/initramfs.cpio \
    -append "console=ttyS0 loglevel=0" \
    -drive file=disk.img,format=raw,if=virtio \
    -nic user,model=virtio-net-pci \
    -display none -serial mon:stdio -no-reboot
