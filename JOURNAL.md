# minios 日誌

AI が minios を 1 周ずつ育てる記録。1 周 = 1 機能 = 1 コミット。

方向: シェル無しで、PID 1 自体が対話できる小さな OS に育てる。

各周で記録すること: やったこと / カーネルに足したもの / サイズ（bzImage, init） / 分かったこと・つまずき

## 周回の手順

1. 次にやることを 1 つ決める
2. init.c / kernel.fragment / initramfs.list を変える
3. `./build.sh && ./test.py` で起動して自動テスト（テストも一緒に足す）
4. 通ったらこの日誌に追記してコミット

## サイズ推移

| 周 | 内容 | bzImage | init |
|---|---|---|---|
| 0 | 起動してメッセージ、キーで終了 | 971,776 | 8,928 |
| 1 | 対話ループと組み込みコマンド 7 個 | 971,776 | 12,912 |
| 2 | /proc・/sys、ps・dmesg・mounts | 1,033,216 | 12,984 |

---

## 周 0: 土台

- 起動してバナーを出し、キー 1 つで終了する PID 1（libc なし、システムコール直叩き）
- カーネルは tinyconfig + 7 オプション（kernel.fragment）
- build.sh: 設定が変わったときだけカーネルを再ビルド（初回約 9〜26 秒、変更なし 0.06 秒）
- test.py: 標準ライブラリだけの小さな expect。QEMU をシリアル越しに操作する
- 分かったこと
  - ACPI が無いので電源断できない。reboot(RESTART) + QEMU の -no-reboot で終了させる
  - gcc は自作 puts を libc の関数と見なし、ループを strlen 呼び出しに置き換える。-ffreestanding が要る
  - シリアルが開かれる前に送った入力は捨てられる
  - /dev/kvm は権限なし。TCG（ソフトウェアエミュレーション）でも起動 1 秒強

## 周 1: 対話ループ

- PID 1 自体がプロンプト `minios# ` を出し、コマンドを受け付ける
- コマンド: help / uname / uptime / mem / echo / clear / shutdown
- libc の代わりを lib.h に分離（syscall、文字列、数値の出力、sysinfo・utsname の構造体）
- カーネル: 変更なし。init: 8,928 → 12,912（+4KB）
- 分かったこと
  - 行編集（エコー、バックスペース）はカーネルの端末（canonical モード）がタダでやってくれる。raw モードにすると全部自前になる。
    シェルの「行を読む」は、実はカーネル側の機能だった。Stable Core に置いておくべき機能の分かりやすい例
  - sysinfo 1 回で uptime とメモリが取れる。/proc が無くても最低限の情報は出せる
  - 起動直後の空きメモリは 64MB 中 55.8MB。カーネル + init で使っているのは約 1.4MB
  - テスト基盤の expect にバグ（一致した後ろの出力を捨てていた）。テスト基盤もテストが要る

## 周 2: /proc と /sys

- 起動時に /proc と /sys をマウント（マウント先のディレクトリは initramfs.list で用意）
- コマンド追加: ps（/proc/<pid>/stat を読む）、dmesg（syslog システムコール）、mounts（/proc/mounts）
- カーネル: PROC_FS, SYSFS を追加。bzImage: 971,776 → 1,033,216（+60KB）
- init: 12,912 → 12,984。ELF は 4KB 境界で並ぶので、小さな追加はサイズにほぼ出ない
- 分かったこと
  - ユーザープロセスは PID 1 の init だけ。ほかの 13 個はカーネルスレッド（kthreadd、kworker、ksoftirqd、kswapd など）。
    何もしない OS でも、カーネルの中では常に十数本が動いている
  - loglevel=0 で画面に出していないだけで、カーネルログ（約 110 行）は syslog でいつでも読める。
    表示とログが分かれているので、起動画面をきれいにしてもデバッグ情報は失われない
  - dmesg の最後に、カーネルが init に渡した環境変数（HOME=/、TERM=linux）が出ている
  - /proc 1 つで +60KB。今のところ、機能を足すとカーネル側が太り、init はほとんど太らない
  - テストの所要時間がマイナスになった。WSL で時計が飛んだらしい。time.monotonic() に変更
