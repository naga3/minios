#!/bin/sh
# カーネル・init・initramfs をビルドして build/ に置く
set -e
cd "$(dirname "$0")"
. ./kbuild-env.sh
KSRC=linux-7.2.8
KOUT=$PWD/build/kernel   # ソースツリーを汚さないよう、カーネルのビルド結果は build/kernel に置く
mkdir -p build

# カーネル: tinyconfig + kernel.fragment（"!NAME" は無効化）。設定が変わったときだけビルドし直す
sed 's/#.*//' kernel.fragment | awk 'NF { if ($1 ~ /^!/) printf "-d %s ", substr($1, 2); else printf "-e %s ", $1 } END {print ""}' > build/fragment.args
if ! cmp -s build/fragment.args build/fragment.args.built || [ ! -f build/bzImage ]; then
    make -C $KSRC O=$KOUT -s tinyconfig
    $KSRC/scripts/config --file $KOUT/.config $(cat build/fragment.args)
    make -C $KSRC O=$KOUT -s olddefconfig
    ./check-config.sh kernel.fragment $KOUT/.config
    make -C $KSRC O=$KOUT -s -j"$(nproc)" bzImage
    cp $KOUT/arch/x86/boot/bzImage build/bzImage
    cp $KOUT/.config build/kernel.config
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

# TinyCC（OS の中の C コンパイラ）。third_party/tinycc は fetch-third-party.sh で取ってくる
if [ ! -x build/tcc/tcc ]; then
    rm -rf build/tcc && mkdir -p build/tcc
    (cd build/tcc && ../../third_party/tinycc/configure --prefix=/usr --extra-ldflags=-static \
        --config-bcheck=no --config-backtrace=no >/dev/null && make -s -j"$(nproc)" tcc libtcc1.a >/dev/null)
    strip build/tcc/tcc
fi

# 同梱する部品のライセンス（NOTICE.md を参照）
mkdir -p build/licenses
cp NOTICE.md build/licenses/NOTICE.md
cp $KSRC/LICENSES/preferred/GPL-2.0 build/licenses/Linux-GPL-2.0
cp $KSRC/LICENSES/exceptions/Linux-syscall-note build/licenses/Linux-syscall-note
cp "$(go env GOROOT)/LICENSE" build/licenses/Go-BSD-3-Clause
cp /usr/share/common-licenses/MPL-2.0 build/licenses/ca-certificates-MPL-2.0
cp third_party/tinycc/COPYING build/licenses/TinyCC-LGPL-2.1

# initramfs
make -C $KSRC O=$KOUT -s usr/gen_init_cpio
$KOUT/usr/gen_init_cpio initramfs.list > build/initramfs.cpio

# 永続ディスク（初回だけ作る。中身を消したいときは disk.img を消す）
if [ ! -f disk.img ]; then
    truncate -s 8M disk.img
    mkfs.minix -3 disk.img >/dev/null
fi
