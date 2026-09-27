#!/bin/sh
# ビルドして QEMU で起動する。Ctrl+C で QEMU ごと終了
set -e
cd "$(dirname "$0")"
./build.sh
exec qemu-system-x86_64 -m 64M \
    -kernel build/bzImage \
    -initrd build/initramfs.cpio \
    -append "console=ttyS0 loglevel=0" \
    -drive file=disk.img,format=raw,if=virtio \
    -display none -serial stdio -monitor none -no-reboot
