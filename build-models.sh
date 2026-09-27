#!/bin/sh
# モデル用のディスクイメージ build/models.img（ext4）を作る。OS の中では /models に読み取り専用でマウントされる。
# mke2fs -d でディレクトリの中身ごとファイルシステムを作るので、root もマウントも要らない
set -e
cd "$(dirname "$0")"
[ -f build/models.img ] && [ build/models.img -nt third_party/models ] && exit 0
rm -rf build/models && mkdir -p build/models
ln third_party/models/*.gguf third_party/models/LICENSE-* build/models/
size=$(du -sm build/models | cut -f1)
rm -f build/models.img
truncate -s $((size + 64))M build/models.img
mke2fs -q -t ext4 -L models -d build/models build/models.img
ls -l build/models.img
