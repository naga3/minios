#!/bin/sh
# check-config.sh <fragment> <.config>: fragment で指定したオプションが本当に有効（"!NAME" は無効、"NAME=VALUE" はその値）になったか確かめる。
# 依存が満たされないオプションは olddefconfig が黙って落とすので、ここで気づく
fragment=$1 config=$2
for opt in $(sed 's/#.*//' "$fragment"); do
    case $opt in
    !*) ! grep -q "^CONFIG_${opt#!}=y" "$config" || { echo "kernel option not disabled: ${opt#!}" >&2; exit 1; } ;;
    *=*) grep -q "^CONFIG_$opt\$" "$config" || { echo "kernel option not set: $opt" >&2; exit 1; } ;;
    *)  grep -q "^CONFIG_$opt=y" "$config" || { echo "kernel option not enabled: $opt" >&2; exit 1; } ;;
    esac
done
