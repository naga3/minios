#!/bin/sh
# ビルドして QEMU で起動する。Ctrl+C はゲストに届く。QEMU を強制終了するには Ctrl+A → X
set -e
cd "$(dirname "$0")"
./build.sh
exec qemu-system-x86_64 -m 64M \
    -kernel build/bzImage \
    -initrd build/initramfs.cpio \
    -append "console=ttyS0 loglevel=0" \
    -drive file=disk.img,format=raw,if=virtio \
    -nic user,model=virtio-net-pci \
    -display none -serial mon:stdio -no-reboot
