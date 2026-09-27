// gobox: Go の外部プログラムを 1 本にまとめたもの（busybox 方式）。
// Go はバイナリごとにランタイム（約 1.5MB）を抱えるので、まとめて 1 つ分で済ませる。
// 呼ばれた名前（argv[0]）でコマンドを選ぶ。/bin/fetch などは gobox へのシンボリックリンク
package main

import (
	"fmt"
	"os"
	"path/filepath"
	"sort"
)

var commands = map[string]func(args []string) int{
	"gohello": gohello,
	"grow":    grow,
	"fetch":   fetch,
}

func main() {
	name := filepath.Base(os.Args[0])
	args := os.Args[1:]
	if name == "gobox" {
		if len(args) == 0 {
			names := make([]string, 0, len(commands))
			for n := range commands {
				names = append(names, n)
			}
			sort.Strings(names)
			fmt.Println("usage: gobox <command> [args...]\ncommands:", names)
			os.Exit(2)
		}
		name, args = args[0], args[1:]
	}
	cmd, ok := commands[name]
	if !ok {
		fmt.Fprintln(os.Stderr, "gobox: unknown command:", name)
		os.Exit(127)
	}
	os.Exit(cmd(args))
}
