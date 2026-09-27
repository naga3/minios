#!/bin/sh
# UEFI で起動する 1 ファイルの OS を作る: build/efi/EFI/BOOT/BOOTX64.EFI
# カーネルに initramfs と起動オプションを埋め込む。FAT のディスク（USB など）にこのファイルを置けば起動する
set -e
cd "$(dirname "$0")"
. ./kbuild-env.sh
./build.sh
KSRC=linux-7.2.8
KOUT=$PWD/build/kernel-efi

# kernel.fragment のうち kernel-efi.fragment で指定し直すものを除き、kernel-efi.fragment を足す
efi_opts=$(sed 's/#.*//' kernel-efi.fragment | awk 'NF {print $1}')
sed 's/#.*//' kernel.fragment | awk 'NF {print $1}' | while read -r opt; do
    name=${opt#!}
    echo "$efi_opts" | grep -qx "${name%%=*}" || echo "$opt"
done > build/fragment-efi.merged
echo "$efi_opts" >> build/fragment-efi.merged
args=$(awk -f fragment-args.awk build/fragment-efi.merged)

make -C $KSRC O=$KOUT -s tinyconfig
# console は最後に書いたものが /dev/console（init の出力先）になる。起動ログはシリアルと画面の両方に出る
$KSRC/scripts/config --file $KOUT/.config $args \
    --set-str CMDLINE "console=tty0 console=ttyS0 loglevel=0" \
    --set-str INITRAMFS_SOURCE "$PWD/build/initramfs.cpio"
make -C $KSRC O=$KOUT -s olddefconfig
./check-config.sh build/fragment-efi.merged $KOUT/.config
make -C $KSRC O=$KOUT -s -j"$(nproc)" bzImage

mkdir -p build/efi/EFI/BOOT
cp $KOUT/arch/x86/boot/bzImage build/efi/EFI/BOOT/BOOTX64.EFI
ls -l build/efi/EFI/BOOT/BOOTX64.EFI
