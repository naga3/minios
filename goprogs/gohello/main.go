// Go で書いた外部プログラム。ランタイムがどのカーネル機能を要るかを確かめる
package main

import (
	"fmt"
	"os"
	"runtime"
	"sync"
	"time"
)

func main() {
	fmt.Printf("hello from Go %s, pid %d, args %v\n", runtime.Version(), os.Getpid(), os.Args[1:])

	// goroutine とチャネル
	var wg sync.WaitGroup
	results := make(chan int, 4)
	for i := 1; i <= 4; i++ {
		wg.Add(1)
		go func(n int) {
			defer wg.Done()
			results <- n * n
		}(i)
	}
	wg.Wait()
	close(results)
	sum := 0
	for r := range results {
		sum += r
	}
	fmt.Println("goroutines: sum of squares =", sum)

	// タイマー
	start := time.Now()
	time.Sleep(50 * time.Millisecond)
	fmt.Println("sleep ok:", time.Since(start) >= 50*time.Millisecond)

	// ファイル
	if err := os.WriteFile("/tmp-go.txt", []byte("written by Go\n"), 0o644); err != nil {
		fmt.Println("write error:", err)
		os.Exit(1)
	}
	b, _ := os.ReadFile("/tmp-go.txt")
	fmt.Print("file: ", string(b))
}
