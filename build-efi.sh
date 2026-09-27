#!/bin/sh
# UEFI で起動する 1 ファイルの OS を作る: build/efi/EFI/BOOT/BOOTX64.EFI
# カーネルに initramfs と起動オプションを埋め込む。FAT のディスク（USB など）にこのファイルを置けば起動する
set -e
cd "$(dirname "$0")"
./build.sh
KSRC=linux-7.2.8
KOUT=$PWD/build/kernel-efi

make -C $KSRC O=$KOUT -s tinyconfig
$KSRC/scripts/config --file $KOUT/.config $(cat build/fragment.args) \
    $(sed 's/#.*//' kernel-efi.fragment | awk 'NF {printf "-e %s ", $1}') \
    --set-str CMDLINE "console=ttyS0 loglevel=0" \
    --set-str INITRAMFS_SOURCE "$PWD/build/initramfs.cpio"
make -C $KSRC O=$KOUT -s olddefconfig
./check-config.sh kernel.fragment $KOUT/.config
./check-config.sh kernel-efi.fragment $KOUT/.config
make -C $KSRC O=$KOUT -s -j"$(nproc)" bzImage

mkdir -p build/efi/EFI/BOOT
cp $KOUT/arch/x86/boot/bzImage build/efi/EFI/BOOT/BOOTX64.EFI
ls -l build/efi/EFI/BOOT/BOOTX64.EFI
