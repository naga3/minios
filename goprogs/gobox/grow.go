// grow <name> <request...>: OS の中のローカル LLM にプログラムを書かせ、コンパイルとテストを通ったものを、
// 人の承認を得てから /data/bin に入れる。出どころ（依頼・モデル・ハッシュ・テスト結果）を /data/bin/.grow/<name> に記録する。
//
// 生成 → コンパイル → テスト を最大 3 回まわす。失敗したら、コンパイルエラーやテストの結果を AI に返して書き直させる。
package main

import (
	"bufio"
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"flag"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"sort"
	"strings"
	"time"
)

const (
	llamaBin    = "/bin/llama-completion"
	ccBin       = "/bin/cc"
	modelsDir   = "/models"
	installDir  = "/data/bin"
	recordDir   = "/data/bin/.grow"
	sourceDir   = "/data/src"
	workDir     = "/tmp/grow"
	maxAttempts = 3
)

// minios の API を教えるシステムプロンプト。1.5B のモデルでも守れるよう、使える関数を並べ、お手本を 1 つ見せる
const systemPrompt = `You write small command-line programs in C for minios, a tiny OS with NO C library.
Rules:
- Write exactly one C file. The first line must be: #include "crt.h"
- Do NOT include any other header. There is no printf, atoi, atol, malloc, stdio.h, stdlib.h or string.h.
- Entry point: int main(int argc, char **argv, char **envp). Return 0 on success.
- Only use the functions listed below. Write any other helper you need yourself.

Available functions (from crt.h):
  void print(const char *s);              // write a string to stdout
  void print_u(unsigned long v);          // write an unsigned number to stdout
  void write_all(int fd, const char *s, size_t n);
  size_t slen(const char *s);             // string length
  int seq(const char *a, const char *b);  // 1 if the strings are equal
  int is_digits(const char *s);           // 1 if s is a non-empty string of 0-9
  long read_file(const char *path, char *buf, size_t size);  // bytes read, or negative on error
  void print_err(const char *what, long err);  // print an error to stderr
  long syscall3(long n, long a, long b, long c); SYS_read, SYS_write: raw system calls
  To read stdin: long n = syscall3(SYS_read, 0, buf, sizeof buf);

Answer with two fenced blocks and nothing else:
1. ` + "```c ... ```" + ` the program
2. ` + "```tests ... ```" + ` 3 test cases, one per line, in the form: arguments => expected output
   Use a single space between arguments. Use (none) when there are no arguments.

Example for the request "echo the arguments in reverse order":
` + "```c" + `
#include "crt.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    for (int i = argc - 1; i >= 1; i--) {
        print(argv[i]);
        if (i > 1) print(" ");
    }
    print("\n");
    return 0;
}
` + "```" + `
` + "```tests" + `
a b c => c b a
x => x
(none) =>
` + "```" + `
`

var (
	nameRe  = regexp.MustCompile(`^[a-z][a-z0-9_-]{0,31}$`)
	codeRe  = regexp.MustCompile("(?s)```c\\s*\n(.*?)```")
	testsRe = regexp.MustCompile("(?s)```tests\\s*\n(.*?)```")
	sysRe   = regexp.MustCompile(`\bSYS_[a-z0-9_]+\b`)
	undefRe = regexp.MustCompile(`unresolved reference to '(\w+)'|implicit declaration of function '(\w+)'`)
)

type testCase struct {
	args     []string
	expected string
}

type testResult struct {
	tc     testCase
	got    string
	passed bool
}

func grow(args []string) int {
	fs := flag.NewFlagSet("grow", flag.ContinueOnError)
	yes := fs.Bool("y", false, "install without asking (for tests)")
	fs.Usage = func() { fmt.Fprintln(os.Stderr, "usage: grow [-y] <name> <what the program should do...>") }
	if err := fs.Parse(args); err != nil || fs.NArg() < 2 {
		if err == nil {
			fs.Usage()
		}
		return 2
	}
	name, request := fs.Arg(0), strings.Join(fs.Args()[1:], " ")
	if !nameRe.MatchString(name) {
		fmt.Fprintln(os.Stderr, "grow: name must be lowercase letters, digits, - or _ (max 32)")
		return 2
	}
	if taken := nameTaken(name); taken != "" {
		fmt.Fprintf(os.Stderr, "grow: %s already exists\n", taken)
		return 2
	}
	model, err := findModel()
	if err != nil {
		fmt.Fprintln(os.Stderr, "grow:", err)
		return 1
	}
	if err := os.MkdirAll(workDir, 0o755); err != nil {
		fmt.Fprintln(os.Stderr, "grow:", err)
		return 1
	}

	prompt := fmt.Sprintf("Write a program named %s: %s", name, request)
	var code string
	var tests []testCase
	var results []testResult
	ok := false
	attempt := 0
	for attempt = 1; attempt <= maxAttempts && !ok; attempt++ {
		fmt.Printf("[%d/%d] generating with %s ...\n", attempt, maxAttempts, filepath.Base(model))
		start := time.Now()
		answer, err := generate(model, prompt, temperatures[attempt-1], attempt)
		if err != nil {
			fmt.Fprintln(os.Stderr, "grow:", err)
			return 1
		}
		fmt.Printf("      %.1fs\n", time.Since(start).Seconds())
		// 各回の依頼と答えを残す（何が起きたかを後から調べられるように）
		os.WriteFile(filepath.Join(workDir, fmt.Sprintf("%s.attempt%d.txt", name, attempt)), []byte(prompt+"\n\n=== answer ===\n"+answer), 0o644)
		code, tests, err = parseAnswer(answer)
		if err != nil {
			fmt.Println("      bad answer:", err)
			prompt = retryPrompt(name, request, answer, "Your answer was not in the required format: "+err.Error())
			continue
		}
		bin := filepath.Join(workDir, name)
		if out, err := compile(code, bin); err != nil {
			fmt.Println("      compile error:")
			fmt.Print(indent(out))
			prompt = retryPrompt(name, request, answer, "The program did not compile:\n"+out+missingFunctionsHint(out))
			continue
		}
		results = runTests(bin, tests)
		failed := 0
		for _, r := range results {
			if !r.passed {
				failed++
			}
		}
		fmt.Printf("      tests: %d/%d passed\n", len(results)-failed, len(results))
		if failed > 0 {
			var b strings.Builder
			for _, r := range results {
				if !r.passed {
					fmt.Fprintf(&b, "arguments %q: expected %q but got %q\n", strings.Join(r.tc.args, " "), r.tc.expected, r.got)
				}
			}
			fmt.Print(indent(b.String()))
			prompt = retryPrompt(name, request, answer, "Some tests failed:\n"+b.String())
			continue
		}
		ok = true
	}
	attempt--
	if !ok {
		fmt.Fprintf(os.Stderr, "grow: gave up after %d attempts\n", maxAttempts)
		return 1
	}

	// 人に見せて承認をもらう
	fmt.Printf("\n--- %s: %s\n", name, request)
	fmt.Print(indent(code))
	fmt.Printf("--- uses system calls: %s\n", strings.Join(syscallsUsed(code), " "))
	fmt.Println("--- tests:")
	for _, r := range results {
		fmt.Printf("    %s => %s  ok\n", argsString(r.tc.args), r.tc.expected)
	}
	if !*yes && !confirm(fmt.Sprintf("install to %s/%s? [y/N] ", installDir, name)) {
		fmt.Println("not installed")
		return 1
	}
	if err := install(name, request, model, code, filepath.Join(workDir, name), attempt, results); err != nil {
		fmt.Fprintln(os.Stderr, "grow:", err)
		return 1
	}
	fmt.Printf("installed %s/%s (record: %s/%s)\n", installDir, name, recordDir, name)
	return 0
}

// シェルの組み込み（init が /etc/builtins に書く）か /bin にある名前は使えない。
// 組み込みは /data/bin より先に選ばれるので、同じ名前で入れても決して実行されない
func nameTaken(name string) string {
	if b, err := os.ReadFile("/etc/builtins"); err == nil {
		for _, line := range strings.Split(string(b), "\n") {
			if line == name {
				return "shell builtin " + name
			}
		}
	}
	if _, err := os.Stat(filepath.Join("/bin", name)); err == nil {
		return "/bin/" + name
	}
	return ""
}

func findModel() (string, error) {
	matches, _ := filepath.Glob(filepath.Join(modelsDir, "*.gguf"))
	if len(matches) == 0 {
		return "", fmt.Errorf("no model in %s (attach models.img)", modelsDir)
	}
	sort.Strings(matches)
	return matches[0], nil
}

// 書き直しのたびに温度を上げる。温度 0 のままだと、同じ間違いを繰り返しやすい（周 24 で atol を 3 回続けた）
var temperatures = [maxAttempts]string{"0", "0.5", "0.9"}

// 乱数の種は試行回数で固定する。同じ依頼からは同じ結果になり、後から再現できる
func generate(model, prompt, temp string, seed int) (string, error) {
	sys := filepath.Join(workDir, "system.txt")
	if err := os.WriteFile(sys, []byte(systemPrompt), 0o644); err != nil {
		return "", err
	}
	cmd := exec.Command(llamaBin, "-m", model, "-sysf", sys, "-p", prompt,
		"-st", "--simple-io", "--temp", temp, "--seed", fmt.Sprint(seed), "-n", "800", "-t", fmt.Sprint(runtime.NumCPU()),
		"--no-display-prompt")
	var out bytes.Buffer
	cmd.Stdout = &out
	cmd.Stderr = io.Discard
	if err := cmd.Run(); err != nil {
		return "", fmt.Errorf("%s: %v", llamaBin, err)
	}
	return out.String(), nil
}

// コンパイラが「無い」と言った関数の名前を拾い、自分で定義するよう具体的に言う
func missingFunctionsHint(out string) string {
	seen := map[string]bool{}
	var names []string
	for _, m := range undefRe.FindAllStringSubmatch(out, -1) {
		n := m[1] + m[2]
		if !seen[n] {
			seen[n] = true
			names = append(names, n)
		}
	}
	if len(names) == 0 {
		return ""
	}
	return fmt.Sprintf("\nThe function(s) %s do NOT exist in minios (there is no C library). "+
		"Do not call them. Write your own helper function in the file instead, before main.", strings.Join(names, ", "))
}

func retryPrompt(name, request, answer, problem string) string {
	return fmt.Sprintf("Write a program named %s: %s\n\nYour previous answer was:\n%s\n\n%s\nWrite a corrected answer in the same format.",
		name, request, strings.TrimSpace(answer), strings.TrimSpace(problem))
}

func parseAnswer(answer string) (string, []testCase, error) {
	c := codeRe.FindStringSubmatch(answer)
	if c == nil {
		return "", nil, fmt.Errorf("no ```c block")
	}
	t := testsRe.FindStringSubmatch(answer)
	if t == nil {
		return "", nil, fmt.Errorf("no ```tests block")
	}
	var tests []testCase
	for _, line := range strings.Split(t[1], "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		left, right, found := strings.Cut(line, "=>")
		if !found {
			return "", nil, fmt.Errorf("test line without =>: %q", line)
		}
		left = strings.TrimSpace(left)
		var args []string
		if left != "(none)" && left != "" {
			args = strings.Fields(left)
		}
		tests = append(tests, testCase{args: args, expected: strings.TrimSpace(right)})
	}
	if len(tests) == 0 {
		return "", nil, fmt.Errorf("no test cases")
	}
	return c[1], tests, nil
}

func compile(code, bin string) (string, error) {
	src := bin + ".c"
	if err := os.WriteFile(src, []byte(code), 0o644); err != nil {
		return "", err
	}
	out, err := exec.Command(ccBin, "-o", bin, src).CombinedOutput()
	return string(out), err
}

// テストは 1 件 5 秒まで。出力は前後の空白を無視して比べる（改行の有無などの揺れを吸収する）
func runTests(bin string, tests []testCase) []testResult {
	var results []testResult
	for _, tc := range tests {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		cmd := exec.CommandContext(ctx, bin, tc.args...)
		cmd.Stdin = strings.NewReader("")
		out, err := cmd.CombinedOutput()
		// cancel より先に調べる。cancel した後の ctx.Err() は、時間切れでなくても nil でなくなる（周 24 で踏んだ）
		timedOut := ctx.Err() == context.DeadlineExceeded
		cancel()
		got := strings.TrimSpace(string(out))
		if timedOut {
			got = "(timed out)"
		} else if err != nil && got == "" {
			got = "(" + err.Error() + ")"
		}
		results = append(results, testResult{tc: tc, got: got, passed: got == tc.expected})
	}
	return results
}

func syscallsUsed(code string) []string {
	seen := map[string]bool{}
	for _, s := range sysRe.FindAllString(code, -1) {
		seen[s] = true
	}
	var list []string
	for s := range seen {
		list = append(list, s)
	}
	sort.Strings(list)
	if len(list) == 0 {
		return []string{"(none directly)"}
	}
	return list
}

func confirm(question string) bool {
	fmt.Print(question)
	line, _ := bufio.NewReader(os.Stdin).ReadString('\n')
	return strings.TrimSpace(strings.ToLower(line)) == "y"
}

func install(name, request, model, code, bin string, attempts int, results []testResult) error {
	for _, d := range []string{installDir, recordDir, sourceDir} {
		if err := os.MkdirAll(d, 0o755); err != nil {
			return err
		}
	}
	binData, err := os.ReadFile(bin)
	if err != nil {
		return err
	}
	modelHash, err := fileSHA256(model)
	if err != nil {
		return err
	}
	src := filepath.Join(sourceDir, name+".c")
	if err := writeFileSync(src, []byte(code), 0o644); err != nil {
		return err
	}
	// 記録は "キー 値" の行。シェル（C）が読んで、実行前にハッシュを確かめられるよう単純にする
	var rec strings.Builder
	fmt.Fprintf(&rec, "name %s\n", name)
	fmt.Fprintf(&rec, "request %s\n", strings.ReplaceAll(request, "\n", " "))
	fmt.Fprintf(&rec, "model %s\n", filepath.Base(model))
	fmt.Fprintf(&rec, "model-sha256 %s\n", modelHash)
	fmt.Fprintf(&rec, "source %s\n", src)
	fmt.Fprintf(&rec, "source-sha256 %s\n", sha256Hex([]byte(code)))
	fmt.Fprintf(&rec, "binary-sha256 %s\n", sha256Hex(binData))
	fmt.Fprintf(&rec, "attempts %d\n", attempts)
	for _, r := range results {
		fmt.Fprintf(&rec, "test %s => %s\n", argsString(r.tc.args), r.tc.expected)
	}
	fmt.Fprintf(&rec, "installed %s\n", time.Now().UTC().Format(time.RFC3339))
	if err := writeFileSync(filepath.Join(recordDir, name), []byte(rec.String()), 0o644); err != nil {
		return err
	}
	return writeFileSync(filepath.Join(installDir, name), binData, 0o755)
}

// 書いてから fsync し、rename で置き換える（途中で電源が落ちても壊れたファイルを残さない。周 4・周 12）
func writeFileSync(path string, data []byte, mode os.FileMode) error {
	tmp := path + ".part"
	f, err := os.OpenFile(tmp, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, mode)
	if err != nil {
		return err
	}
	if _, err = f.Write(data); err == nil {
		err = f.Sync()
	}
	if cerr := f.Close(); err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Rename(tmp, path)
	}
	if err != nil {
		os.Remove(tmp)
	}
	return err
}

func fileSHA256(path string) (string, error) {
	f, err := os.Open(path)
	if err != nil {
		return "", err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return "", err
	}
	return hex.EncodeToString(h.Sum(nil)), nil
}

func sha256Hex(b []byte) string {
	s := sha256.Sum256(b)
	return hex.EncodeToString(s[:])
}

func argsString(args []string) string {
	if len(args) == 0 {
		return "(none)"
	}
	return strings.Join(args, " ")
}

func indent(s string) string {
	var b strings.Builder
	for _, line := range strings.Split(strings.TrimRight(s, "\n"), "\n") {
		b.WriteString("    " + line + "\n")
	}
	return b.String()
}
