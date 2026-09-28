# NOTICE

minios のバイナリ（build.sh が作る bzImage と initramfs、build-efi.sh が作る BOOTX64.EFI）には、次のものが入っている。
ライセンスの本文は、OS の中の /licenses にも同梱している。

| 部品 | 入っている場所 | ライセンス | 出どころ |
|---|---|---|---|
| Linux カーネル 7.2.8 | bzImage、BOOTX64.EFI | GPL-2.0（Linux-syscall-note 付き） | https://cdn.kernel.org/pub/linux/kernel/v7.x/linux-7.2.8.tar.xz |
| Go の標準ライブラリとランタイム（go1.26.4） | /bin/gobox | BSD-3-Clause（Copyright The Go Authors） | https://go.dev/ |
| CA 証明書の束（Mozilla の certdata.txt から作られたもの） | /etc/ssl/certs/ca-certificates.crt | MPL-2.0（Copyright Mozilla Contributors） | ビルドしたホストの Ubuntu の ca-certificates パッケージ |
| TinyCC（0.9.28rc、コミット 9db1105c） | /bin/tcc、/usr/lib/tcc/libtcc1.a | LGPL-2.1 | https://github.com/TinyCC/tinycc（公式 repo.or.cz の開発ブランチ mob のミラー）。fetch-third-party.sh で取得 |
| glibc（tcc と llama-completion に静的リンク） | /bin/tcc、/bin/llama-completion | LGPL-2.1 | ビルドしたホストの Ubuntu の libc6-dev |
| llama.cpp（v0.5.0、コミット 7fe450e1） | /bin/llama-completion | MIT | https://github.com/ggml-org/llama.cpp 。fetch-third-party.sh で取得 |
| cpp-httplib、nlohmann/json（llama.cpp が同梱） | /bin/llama-completion | MIT | llama.cpp の vendor/ |
| libstdc++（llama-completion に静的リンク） | /bin/llama-completion | GPL-3.0 + GCC Runtime Library Exception（配布の義務なし） | ビルドしたホストの Ubuntu の GCC 13 |
| Qwen2.5-Coder-1.5B-Instruct（4bit 量子化、GGUF） | build/models.img（/models。OS のイメージとは別） | Apache-2.0（本文はディスクの中の LICENSE-Qwen2.5-Coder-1.5B-Instruct） | https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF 。fetch-third-party.sh でリビジョンとハッシュを固定して取得 |
| Qwen2.5-Coder-7B-Instruct（4bit 量子化、GGUF、2 つに分けたもの） | build/models.img（/models） | Apache-2.0（本文は 1.5B と同じ） | https://huggingface.co/Qwen/Qwen2.5-Coder-7B-Instruct-GGUF 。fetch-third-party.sh でリビジョンとハッシュを固定して取得 |
| minios 自身のコード（init、シェル、progs、gobox のコード、ビルド・テストのスクリプト） | /init、/bin | 未定 | このリポジトリ |

## Linux カーネルのソース（GPL-2.0 の義務）

BOOTX64.EFI や bzImage を配るときは、対応するソースを受け取れるようにする必要がある。
対応するソースは、上の kernel.org の tarball（改変なし）と、このリポジトリの次のファイルで全部になる。

- kernel.fragment、kernel-efi.fragment（tinyconfig に足す・削る設定）
- build.sh、build-efi.sh、check-config.sh、kbuild-env.sh（設定の作り方とビルドの手順）
- initramfs.list（BOOTX64.EFI に埋め込む initramfs の中身）

## TinyCC と glibc のソース（LGPL-2.1 の義務）

/bin/tcc は TinyCC を、/bin/llama-completion は llama.cpp を、それぞれ glibc と静的リンクしたもの。TinyCC のソースは上のコミット（改変なし）、
ビルドの手順は build.sh にある。glibc は Ubuntu 24.04 の libc6-dev（2.39）で、ソースは Ubuntu から入手できる。
LGPL は、利用者が glibc を差し替えて再リンクできることを求めるので、TinyCC のソースとビルド手順を公開しておく必要がある。

## ビルドにだけ使うもの（配布物には入らない）

- GCC、binutils、Go のツールチェイン、CMake（llama.cpp のビルド。fetch-third-party.sh で取得）
- QEMU、OVMF（テストで起動するときだけ）
