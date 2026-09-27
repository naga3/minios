#!/bin/sh
# カーネル・init・initramfs をビルドして build/ に置く
set -e
cd "$(dirname "$0")"
KSRC=linux-7.2.8
mkdir -p build

# カーネル: tinyconfig + kernel.fragment。設定が変わったときだけビルドし直す
sed 's/#.*//' kernel.fragment | awk 'NF {printf "-e %s ", $1} END {print ""}' > build/fragment.args
if ! cmp -s build/fragment.args build/fragment.args.built || [ ! -f build/bzImage ]; then
    make -C $KSRC -s tinyconfig
    $KSRC/scripts/config --file $KSRC/.config $(cat build/fragment.args)
    make -C $KSRC -s olddefconfig
    for opt in $(sed 's/#.*//' kernel.fragment); do
        grep -q "^CONFIG_$opt=y" $KSRC/.config || { echo "kernel option not enabled: $opt" >&2; exit 1; }
    done
    make -C $KSRC -s -j"$(nproc)" bzImage
    cp $KSRC/arch/x86/boot/bzImage build/bzImage
    cp $KSRC/.config build/kernel.config
    cp build/fragment.args build/fragment.args.built
fi

# init: libc なしの静的バイナリ
gcc -static -nostdlib -Os -ffreestanding -fno-stack-protector -fno-asynchronous-unwind-tables \
    -Wall -Werror -o build/init init.c
strip build/init

# initramfs
$KSRC/usr/gen_init_cpio initramfs.list > build/initramfs.cpio
