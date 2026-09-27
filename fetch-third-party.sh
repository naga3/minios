#!/bin/sh
# サードパーティのソースを、決まった版で third_party/ に取ってくる（git 管理外）。ライセンスは NOTICE.md
set -e
cd "$(dirname "$0")"
mkdir -p third_party

# fetch <dir> <url> <commit>
fetch() {
    if [ ! -d "third_party/$1/.git" ]; then
        git clone --quiet --filter=blob:none "$2" "third_party/$1"
    fi
    if [ "$(git -C "third_party/$1" rev-parse HEAD)" != "$3" ]; then
        git -C "third_party/$1" fetch --quiet --depth 1 origin "$3"
        git -C "third_party/$1" checkout --quiet "$3"
    fi
}

# TinyCC（LGPL-2.1）。公式は repo.or.cz、これは開発ブランチ mob の GitHub ミラー
fetch tinycc https://github.com/TinyCC/tinycc 9db1105c32afd3dcf0c28b8186f08e63c761b2b5
