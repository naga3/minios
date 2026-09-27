#!/bin/sh
# カーネル・init・initramfs をビルドして build/ に置く
set -e
cd "$(dirname "$0")"
KSRC=linux-7.2.8
mkdir -p build

# カーネル: tinyconfig + kernel.fragment（"!NAME" は無効化）。設定が変わったときだけビルドし直す
sed 's/#.*//' kernel.fragment | awk 'NF { if ($1 ~ /^!/) printf "-d %s ", substr($1, 2); else printf "-e %s ", $1 } END {print ""}' > build/fragment.args
if ! cmp -s build/fragment.args build/fragment.args.built || [ ! -f build/bzImage ]; then
    make -C $KSRC -s tinyconfig
    $KSRC/scripts/config --file $KSRC/.config $(cat build/fragment.args)
    make -C $KSRC -s olddefconfig
    for opt in $(sed 's/#.*//' kernel.fragment); do
        case $opt in
        !*) ! grep -q "^CONFIG_${opt#!}=y" $KSRC/.config || { echo "kernel option not disabled: ${opt#!}" >&2; exit 1; } ;;
        *)  grep -q "^CONFIG_$opt=y" $KSRC/.config || { echo "kernel option not enabled: $opt" >&2; exit 1; } ;;
        esac
    done
    make -C $KSRC -s -j"$(nproc)" bzImage
    cp $KSRC/arch/x86/boot/bzImage build/bzImage
    cp $KSRC/.config build/kernel.config
    cp build/fragment.args build/fragment.args.built
fi

# libc なしの静的バイナリ。lib.h はヘッダだけのライブラリなので、使わない関数の警告は切る
CFLAGS="-static -nostdlib -Os -ffreestanding -fno-stack-protector -fno-asynchronous-unwind-tables -Wall -Werror -Wno-unused-function"

# init
gcc $CFLAGS -o build/init init.c
strip build/init

# 外部プログラム（progs/*.c → build/bin/）
mkdir -p build/bin
for src in progs/*.c; do
    name=$(basename "$src" .c)
    gcc $CFLAGS -o "build/bin/$name" "$src"
    strip "build/bin/$name"
done

# Go の外部プログラム（goprogs/<name>/ → build/bin/<name>）。libc なしの静的バイナリにする
for dir in goprogs/*/; do
    name=$(basename "$dir")
    (cd "$dir" && CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -trimpath -ldflags="-s -w" -o "../../build/bin/$name" .)
done

# CA 証明書（HTTPS の検証用）。ホストの束をそのまま使う
cp /etc/ssl/certs/ca-certificates.crt build/ca-certificates.crt

# initramfs
$KSRC/usr/gen_init_cpio initramfs.list > build/initramfs.cpio

# 永続ディスク（初回だけ作る。中身を消したいときは disk.img を消す）
if [ ! -f disk.img ]; then
    truncate -s 8M disk.img
    mkfs.minix -3 disk.img >/dev/null
fi
