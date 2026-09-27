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

# download <path> <url> <sha256>: ハッシュが合うときだけ置く
download() {
    if [ -f "$1" ] && echo "$3  $1" | sha256sum -c --quiet 2>/dev/null; then return; fi
    mkdir -p "$(dirname "$1")"
    curl -fL --progress-bar -o "$1.part" "$2"
    echo "$3  $1.part" | sha256sum -c --quiet
    mv "$1.part" "$1"
}

# llama.cpp（MIT）v0.5.0。OS の中でローカル LLM を動かす
fetch llama.cpp https://github.com/ggml-org/llama.cpp 7fe450e19305b828c199d602c23a8337aaa1f03b

# CMake（BSD-3-Clause）。llama.cpp のビルドにだけ使う（配布物には入らない）
download third_party/cmake-4.4.3-linux-x86_64.tar.gz \
    https://github.com/Kitware/CMake/releases/download/v4.4.3/cmake-4.4.3-linux-x86_64.tar.gz \
    d6c83076c575bc00b823522ac974bda66d0af05d6ddc30e739c12385cf32c6cc
[ -x third_party/cmake/bin/cmake ] || { mkdir -p third_party/cmake && tar xzf third_party/cmake-4.4.3-linux-x86_64.tar.gz -C third_party/cmake --strip-components=1; }

# モデル: Qwen2.5-Coder-1.5B-Instruct（Apache-2.0）の 4bit 量子化（1.1GB）。リビジョンとハッシュを固定する
download third_party/models/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf \
    https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/f86cb2c1fa58255f8052cc32aeede1b7482d4361/qwen2.5-coder-1.5b-instruct-q4_k_m.gguf \
    cc324af070c2ecbfd324a30884d2f951a7ff756aba85cb811a6ec436933bb046
download third_party/models/LICENSE-Qwen2.5-Coder-1.5B-Instruct \
    https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/resolve/f86cb2c1fa58255f8052cc32aeede1b7482d4361/LICENSE \
    832dd9e00a68dd83b3c3fb9f5588dad7dcf337a0db50f7d9483f310cd292e92e
