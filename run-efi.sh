#!/bin/sh
# UEFI 版を、実機と同じ経路（UEFI ファームウェア → FAT ディスク上の BOOTX64.EFI）で QEMU から起動する。
#   ./run-efi.sh       シリアルだけ（この端末で操作する）
#   ./run-efi.sh gui   画面のウィンドウも開き、USB キーボードで操作できる（WSL なら WSLg で表示される）
# Ctrl+C はゲストに届く。QEMU を強制終了するには Ctrl+A → X
set -e
cd "$(dirname "$0")"
./build-efi.sh
[ -f build/ovmf_vars.fd ] || cp /usr/share/OVMF/OVMF_VARS_4M.fd build/ovmf_vars.fd
display="-display none"
[ "$1" = gui ] && display="-display gtk -device qemu-xhci -device usb-kbd"
# KVM が使えれば使う。使えなければソフトウェアエミュレーション
accel="-accel tcg"
[ -r /dev/kvm ] && [ -w /dev/kvm ] && accel="-accel kvm -cpu host"
exec qemu-system-x86_64 $accel -m 256M \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,file=build/ovmf_vars.fd \
    -drive file=fat:rw:build/efi,format=raw \
    -drive file=disk.img,format=raw,if=virtio \
    -nic user,model=virtio-net-pci \
    $display -serial mon:stdio
