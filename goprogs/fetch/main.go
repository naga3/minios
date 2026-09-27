// fetch <URL>: HTTP(S) で GET し、ステータスと本文の先頭を出す
package main

import (
	"fmt"
	"io"
	"net/http"
	"os"
	"time"
)

func main() {
	if len(os.Args) != 2 {
		fmt.Fprintln(os.Stderr, "usage: fetch <url>")
		os.Exit(2)
	}
	client := &http.Client{Timeout: 10 * time.Second}
	resp, err := client.Get(os.Args[1])
	if err != nil {
		fmt.Fprintln(os.Stderr, "fetch:", err)
		os.Exit(1)
	}
	defer resp.Body.Close()
	fmt.Println(resp.Proto, resp.Status)
	body, _ := io.ReadAll(io.LimitReader(resp.Body, 300))
	fmt.Println(string(body))
}
