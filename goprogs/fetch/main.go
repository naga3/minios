// fetch [-o <path>] <URL>: HTTP(S) で GET する。
// -o なしならステータスと本文の先頭を出す。-o ありなら本文をそのままファイルに保存する（2xx 以外は失敗）
package main

import (
	"flag"
	"fmt"
	"io"
	"net/http"
	"os"
	"time"
)

func main() {
	out := flag.String("o", "", "save the body to this path")
	flag.Usage = func() { fmt.Fprintln(os.Stderr, "usage: fetch [-o path] <url>") }
	flag.Parse()
	if flag.NArg() != 1 {
		flag.Usage()
		os.Exit(2)
	}
	url := flag.Arg(0)

	client := &http.Client{Timeout: 30 * time.Second}
	resp, err := client.Get(url)
	if err != nil {
		fmt.Fprintln(os.Stderr, "fetch:", err)
		os.Exit(1)
	}
	defer resp.Body.Close()

	if *out == "" {
		fmt.Println(resp.Proto, resp.Status)
		body, _ := io.ReadAll(io.LimitReader(resp.Body, 300))
		fmt.Println(string(body))
		return
	}

	if resp.StatusCode/100 != 2 {
		fmt.Fprintln(os.Stderr, "fetch:", url, resp.Status)
		os.Exit(1)
	}
	// 途中で失敗しても壊れたファイルを残さないよう、一時ファイルに書いてから置き換える
	tmp := *out + ".part"
	f, err := os.Create(tmp)
	if err != nil {
		fmt.Fprintln(os.Stderr, "fetch:", err)
		os.Exit(1)
	}
	n, err := io.Copy(f, resp.Body)
	if err == nil {
		err = f.Sync()
	}
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Rename(tmp, *out)
	}
	if err != nil {
		os.Remove(tmp)
		fmt.Fprintln(os.Stderr, "fetch:", err)
		os.Exit(1)
	}
	fmt.Printf("saved %d bytes to %s\n", n, *out)
}
