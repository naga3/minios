#!/bin/sh
# tiny（アセンブリだけの自作カーネル。320 × 200・256 色の画面と PS/2 キーボード）をビルドして QEMU で起動する。
# 画面はウィンドウに出る（WSL なら WSLg）。ウィンドウを閉じると終わる。
# Ctrl+Alt+F でフルスクリーンの切り替え、Ctrl+Alt+G でウィンドウに取られたマウスとキーボードを返してもらう
# 音は AC97（PCI のサウンドカード）から PulseAudio（WSL なら WSLg）へ。PulseAudio が無ければ鳴らさない
set -e
cd "$(dirname "$0")"
tiny/build.sh
# QEMU は既定で 44,100 Hz に変換して出し、その変換（線形補間）が雑音を足す。こちらと同じ 48,000 Hz のまま渡す。
# 録音（マイク）は使わないので開かない（in.voices=0）。起動時の「pulseaudio: set_sink_input_volume() failed」は、
# AC97 が電源を入れた瞬間に音量を伝えようとして、PulseAudio の準備が間に合わないときに出る（害はない）
audio="none,id=snd,out.frequency=48000,in.voices=0"
if [ -n "$PULSE_SERVER" ] || [ -S "${XDG_RUNTIME_DIR:-/nonexistent}/pulse/native" ]; then audio="pa,id=snd,out.frequency=48000,in.voices=0"; fi
# KVM が使えれば使う。使えなければソフトウェアエミュレーション
accel="-accel tcg"
[ -r /dev/kvm ] && [ -w /dev/kvm ] && accel="-accel kvm -cpu host"
exec qemu-system-x86_64 $accel -kernel build/tiny/kernel.elf -display gtk -serial none -audiodev $audio -device AC97,audiodev=snd
