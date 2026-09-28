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
	sandboxBin  = "/bin/sandbox"
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
- Do NOT include any other header. There is no printf, malloc, stdio.h, stdlib.h or string.h.
- Entry point: int main(int argc, char **argv, char **envp). Return 0 on success.
- Only use the functions listed below. Write any other helper you need yourself.

Available functions (from crt.h):
  void print(const char *s);              // write a string to stdout
  void print_u(unsigned long v);          // write an unsigned number to stdout
  void print_i(long v);                   // write a signed number to stdout (use this for negative numbers)
  void print_c(char c);                   // write one character to stdout (print takes a string, not a char)
  void write_all(int fd, const char *s, size_t n);
  long atol(const char *s);               // string to number, handles a leading - (also: int atoi)
  size_t slen(const char *s);             // string length
  int seq(const char *a, const char *b);  // 1 if the strings are equal
  int is_digits(const char *s);           // 1 if s is a non-empty string of 0-9
  void str_copy(char *dst, size_t size, const char *src);  // copy a string (NUL-terminated)
  char *next_word(char **p);              // split a line into words: char *p = line; char *w; while ((w = next_word(&p))) ...
                                          // (it writes NUL into the line; there is no strtok)
  long read_line(char *buf, size_t size); // read one line from stdin without the newline; -1 at the end of input
  long read_file(const char *path, char *buf, size_t size);  // bytes read, or negative on error
  long write_file(const char *path, const char *data, size_t n);  // create or replace a file; 0 or negative on error
  void print_err(const char *what, long err);  // print an error to stderr
  long syscall3(long n, long a, long b, long c); SYS_read, SYS_write: raw system calls
Programs that read commands from stdin must not print prompts or banners unless asked (tests compare stdout exactly).

Answer with two fenced blocks and nothing else:
1. ` + "```c ... ```" + ` the program
2. ` + "```tests ... ```" + ` 3 test cases, one per line, in the form: arguments => expected output
   Use a single space between arguments. Use (none) when there are no arguments.
   If the program reads stdin, add | and the input: arguments | input => expected output   (write \n between input lines)
   If the program writes a file, add the file check at the end: ... => expected output ; file NAME = expected content
   Tests run in an empty directory, so use file names without a directory. Write \n for newlines in outputs and contents.

Example for the request "print the length of each argument":
` + "```c" + `
#include "crt.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    for (int i = 1; i < argc; i++) {
        print_u(slen(argv[i]));
        if (i < argc - 1) print(" ");
    }
    print("\n");
    return 0;
}
` + "```" + `
` + "```tests" + `
abc de => 3 2
hello => 5
(none) =>
` + "```" + `
Do not copy the example tests; write tests for THIS program.
`

// 独立したテストを書かせるシステムプロンプト。プログラムは見せず、依頼文だけから書かせる
// （コードを書いた AI が自分でテストも書くと、コードに合わせたテストになりうる）
const testerPrompt = `You write test cases for a small command-line program. You cannot see the program; you only know what it should do.
Each test case is one line: arguments => expected output (exactly what the program prints).
Arguments are separated by single spaces and cannot contain spaces. Use (none) when there are no arguments.
If the program reads stdin: arguments | input => expected output   (write \n between input lines)
If the program writes a file, add at the end: ; file NAME = expected content   (tests run in an empty directory)
Write 3 test cases for THIS program, including at least one edge case. Answer with one fenced block and nothing else.
Think about what this program prints for each input. Do not copy the example below; it is for a different program.

Example for the request "print how many arguments there are":
` + "```tests" + `
a b c => 3
hello => 1
(none) => 0
` + "```" + `
`

var (
	nameRe  = regexp.MustCompile(`^[a-z][a-z0-9_-]{0,31}$`)
	codeRe  = regexp.MustCompile("(?s)```c\\s*\n(.*?)```")
	testsRe = regexp.MustCompile("(?s)```tests\\s*\n(.*?)```")
	sysRe   = regexp.MustCompile(`\bSYS_[a-z0-9_]+\b`)
	splitRe = regexp.MustCompile(`^(.*)-\d{5}-of-(\d{5})\.gguf$`)
	callRe  = regexp.MustCompile(`\b([a-z_0-9]+)\s*\(`)
	undefRe = regexp.MustCompile(`unresolved reference to '(\w+)'|implicit declaration of function '(\w+)'`)
)

type fileCheck struct {
	name, content string
}

// 1 行のテスト: 引数 [| 標準入力] => 期待する出力 [; file 名前 = 期待する中身]...
type testCase struct {
	args     []string
	stdin    string
	expected string
	files    []fileCheck
}

// テストを 1 行の書式に戻す（表示と記録のため）
func (tc testCase) String() string {
	var b strings.Builder
	b.WriteString(argsString(tc.args))
	if tc.stdin != "" {
		b.WriteString(" | " + escapeNL(tc.stdin))
	}
	b.WriteString(" => " + escapeNL(tc.expected))
	for _, f := range tc.files {
		b.WriteString(" ; file " + f.name + " = " + escapeNL(f.content))
	}
	return b.String()
}

func escapeNL(s string) string   { return strings.ReplaceAll(s, "\n", `\n`) }
func unescapeNL(s string) string { return strings.ReplaceAll(s, `\n`, "\n") }

// 出力や中身を比べるときは、各行の行末の空白と、前後の空行を無視する
func normalize(s string) string {
	lines := strings.Split(strings.ReplaceAll(s, "\r\n", "\n"), "\n")
	for i, l := range lines {
		lines[i] = strings.TrimRight(l, " \t")
	}
	return strings.Trim(strings.Join(lines, "\n"), "\n")
}

type candidate struct {
	code     string
	results  []testResult
	syscalls []string
	attempt  int
}

type testResult struct {
	tc           testCase
	independent  bool // コードを見ずに書かれたテストか
	got          string
	fileProblems []string
	passed       bool
}

func grow(args []string) int {
	fs := flag.NewFlagSet("grow", flag.ContinueOnError)
	yes := fs.Bool("y", false, "install without asking (for tests)")
	showLog := fs.Bool("log", false, "show the versions of a program")
	undo := fs.Bool("undo", false, "go back to the previous version")
	modelName := fs.String("model", "", "use the model whose file name contains this (default: the largest)")
	fs.Usage = func() {
		fmt.Fprintln(os.Stderr, "usage: grow [-y] [-model 1.5b] <name> <what it should do, or how to change it...>\n       grow -log <name>\n       grow -undo <name>")
	}
	if err := fs.Parse(args); err != nil || fs.NArg() < 1 {
		if err == nil {
			fs.Usage()
		}
		return 2
	}
	name := fs.Arg(0)
	if !nameRe.MatchString(name) {
		fmt.Fprintln(os.Stderr, "grow: name must be lowercase letters, digits, - or _ (max 32)")
		return 2
	}
	if *showLog {
		return growLog(name)
	}
	if *undo {
		return growUndo(name)
	}
	if fs.NArg() < 2 {
		fs.Usage()
		return 2
	}
	request := strings.Join(fs.Args()[1:], " ")
	if taken := nameTaken(name); taken != "" {
		fmt.Fprintf(os.Stderr, "grow: %s already exists\n", taken)
		return 2
	}
	// もうあれば育てる（今のソースと、これまでの依頼を AI に渡して、変えてもらう）
	cur, err := loadCurrent(name)
	if err != nil {
		fmt.Fprintln(os.Stderr, "grow:", err)
		return 1
	}
	model, err := findModel(*modelName)
	if err != nil {
		fmt.Fprintln(os.Stderr, "grow:", err)
		return 1
	}
	if err := os.MkdirAll(workDir, 0o755); err != nil {
		fmt.Fprintln(os.Stderr, "grow:", err)
		return 1
	}
	// 独立したテストに渡す「何をするプログラムか」。育てるときは、これまでの依頼に今回の依頼を足す
	spec := request
	if cur != nil {
		spec = strings.Join(append(cur.history, "Change: "+request), ". ")
		fmt.Printf("[grow] %s is at version %d. Changing it: %s\n", name, cur.version, request)
	}

	// 先に、コードを見せずに依頼文だけから独立したテストを書かせる
	fmt.Printf("[tests] writing independent tests with %s ...\n", filepath.Base(model))
	testerRequest := fmt.Sprintf("The program is named %s. It should: %s", name, request)
	if cur != nil {
		// 今の振る舞いと頼まれた変更を分けて伝え、変更の後の振る舞いを書かせる（周 29 では、テスト係が変更を無視して元の振る舞いを期待し続けた）
		testerRequest = fmt.Sprintf("The program is named %s. Current behavior: %s. Requested change: %s. "+
			"Write tests for the behavior AFTER the change.", name, strings.Join(cur.history, ". "), request)
	}
	independent, err := generateIndependentTests(model, testerRequest)
	if err != nil {
		fmt.Fprintln(os.Stderr, "grow:", err)
		return 1
	}
	for _, tc := range independent {
		fmt.Printf("      %s\n", tc)
	}

	prompt := fmt.Sprintf("Write a program named %s: %s", name, request)
	if cur != nil {
		prompt = changePrompt(name, cur, request)
	}
	var code string
	var tests []testCase
	var results []testResult
	var syscalls []string
	var last *candidate
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
			prompt = retryPrompt(name, spec, answer, "Your answer was not in the required format: "+err.Error())
			continue
		}
		unchanged := cur != nil && strings.TrimSpace(code) == strings.TrimSpace(cur.source)
		bin := filepath.Join(workDir, name)
		if out, err := compile(code, bin); err != nil {
			fmt.Println("      compile error:")
			fmt.Print(indent(out))
			prompt = retryPrompt(name, spec, answer, "The program did not compile:\n"+out+missingFunctionsHint(out))
			continue
		}
		syscalls = syscallsUsed(code)
		results = append(runTests(bin, syscalls, tests, false), runTests(bin, syscalls, independent, true)...)
		failed := 0
		for _, r := range results {
			if !r.passed {
				failed++
			}
		}
		fmt.Printf("      tests: %d/%d passed (own %d, independent %d)\n", len(results)-failed, len(results), len(tests), len(independent))
		// 育てるのに前の版と同じコードを返してきたとき: 新しいテストにも全部通るなら、もうそうなっている（変える必要が無い）。
		// 通らないなら、変わっていない（周 28 では、テストが誤っていて同じコードが version 2 として入った）
		if unchanged && failed == 0 {
			fmt.Printf("\n%s already does that (version %d passes the new tests); nothing to change\n", name, cur.version)
			return 0
		}
		if unchanged {
			fmt.Println("      the program did not change")
			prompt = retryPrompt(name, spec, answer, "Your program is exactly the same as the current version. It must change: "+request)
			continue
		}
		// コンパイルでき、前の版から変わっている案を覚えておく（食い違いが残ったとき、人に見せる）
		last = &candidate{code: code, results: results, syscalls: syscalls, attempt: attempt}
		if data, err := os.ReadFile(bin); err == nil {
			os.WriteFile(bin+".candidate", data, 0o755)
		}
		if failed > 0 {
			var b strings.Builder
			for _, r := range results {
				if !r.passed {
					// テストの書式（=>）と紛れない文章にする。紛れると、AI がこの文面をテストの欄に写してくる（周 27）
					input := ""
					if r.tc.stdin != "" {
						input = fmt.Sprintf(" and the input [%s]", escapeNL(strings.TrimSuffix(r.tc.stdin, "\n")))
					}
					if r.got != normalize(r.tc.expected) {
						fmt.Fprintf(&b, "- With the arguments [%s]%s the program printed [%s], but it must print [%s].%s\n",
							strings.Join(r.tc.args, " "), input, escapeNL(r.got), escapeNL(r.tc.expected), testLabel(r))
					}
					for _, p := range r.fileProblems {
						fmt.Fprintf(&b, "- With the arguments [%s]%s %s.%s\n", strings.Join(r.tc.args, " "), input, p, testLabel(r))
					}
				}
			}
			fmt.Print(indent(b.String()))
			prompt = retryPrompt(name, spec, answer, "Some tests failed:\n"+b.String()+
				"Either the program or the test is wrong. Check each one against what the program should do. "+
				"If your own test was wrong, fix the test and keep the program.")
			continue
		}
		ok = true
	}
	attempt--
	bin := filepath.Join(workDir, name)
	if !ok {
		if last == nil {
			fmt.Fprintf(os.Stderr, "grow: gave up after %d attempts\n", maxAttempts)
			return 1
		}
		// テストと食い違ったままの案が残った。テストの方が間違っていることもある（周 27、周 29）ので、最後は人が決める
		fmt.Printf("\nthe program and some tests still disagree after %d attempts; you decide (marked with x below)\n", maxAttempts)
		code, results, syscalls, attempt = last.code, last.results, last.syscalls, last.attempt
		bin += ".candidate"
	}

	// 人に見せて承認をもらう
	if cur != nil {
		fmt.Printf("\n--- %s version %d -> %d: %s\n", name, cur.version, cur.version+1, request)
		fmt.Print(lineDiff(cur.source, code))
	} else {
		fmt.Printf("\n--- %s: %s\n", name, request)
		fmt.Print(indent(code))
	}
	fmt.Printf("--- allowed system calls (enforced with seccomp): %s\n", strings.Join(syscalls, " "))
	fmt.Println("--- tests:")
	for _, r := range results {
		if r.passed {
			fmt.Printf("    %s  ok%s\n", r.tc, testLabel(r))
		} else {
			fmt.Printf("  x %s  but the program printed %s%s\n", r.tc, escapeNL(r.got), testLabel(r))
			for _, p := range r.fileProblems {
				fmt.Printf("      (%s)\n", p)
			}
		}
	}
	question := fmt.Sprintf("install to %s/%s? [y/N] ", installDir, name)
	if !ok {
		question = fmt.Sprintf("install to %s/%s anyway (if you think the tests marked x are wrong)? [y/N] ", installDir, name)
	}
	// -y でも、テストと食い違ったものは入れない（安全側に倒す）
	if (!*yes || !ok) && !confirm(question) {
		fmt.Println("not installed")
		return 1
	}
	version, history := 1, []string{request}
	if cur != nil {
		version, history = cur.version+1, append(cur.history, request)
	}
	if err := install(name, request, model, code, bin, attempt, syscalls, results, version, history); err != nil {
		fmt.Fprintln(os.Stderr, "grow:", err)
		return 1
	}
	fmt.Printf("installed %s/%s version %d (record: %s/%s)\n", installDir, name, version, recordDir, name)
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

// /models のモデルから選ぶ。want があればファイル名にそれを含むもの、無ければ一番大きいもの（一番賢いはず）。
// 分けて置いたモデル（-00001-of-00002.gguf など）は 1 つ目を指し、合計の大きさで比べる
func findModel(want string) (string, error) {
	files, _ := filepath.Glob(filepath.Join(modelsDir, "*.gguf"))
	sizes := map[string]int64{}
	for _, f := range files {
		base := f
		if m := splitRe.FindStringSubmatch(f); m != nil { // 2 つ目以降は 1 つ目に足す
			base = m[1] + "-00001-of-" + m[2] + ".gguf"
		}
		if st, err := os.Stat(f); err == nil {
			sizes[base] += st.Size()
		}
	}
	best, bestSize := "", int64(-1)
	for f, size := range sizes {
		if want != "" && !strings.Contains(filepath.Base(f), want) {
			continue
		}
		if size > bestSize {
			best, bestSize = f, size
		}
	}
	if best == "" {
		if want != "" {
			return "", fmt.Errorf("no model matching %q in %s", want, modelsDir)
		}
		return "", fmt.Errorf("no model in %s (attach models.img)", modelsDir)
	}
	return best, nil
}

// 書き直しのたびに温度を上げる。温度 0 のままだと、同じ間違いを繰り返しやすい（周 24 で atol を 3 回続けた）
var temperatures = [maxAttempts]string{"0", "0.5", "0.9"}

// 乱数の種は試行回数で固定する。同じ依頼からは同じ結果になり、後から再現できる
func generate(model, prompt, temp string, seed int) (string, error) {
	return generateWith(model, systemPrompt, prompt, temp, seed, 2000)
}

func generateWith(model, system, prompt, temp string, seed, maxTokens int) (string, error) {
	// システムプロンプトごと・モデルごとに、先頭部分の計算結果をキャッシュして使い回す（7B では毎回 15〜20 秒かかっていた）
	key := sha256Hex([]byte(model + "\x00" + system))[:16]
	sys := filepath.Join(workDir, "system-"+key+".txt")
	if err := os.WriteFile(sys, []byte(system), 0o644); err != nil {
		return "", err
	}
	cache := filepath.Join(workDir, "cache-"+key+".bin")
	cmd := exec.Command(llamaBin, "-m", model, "-sysf", sys, "-p", prompt, "--prompt-cache", cache,
		// 文脈は 8192 トークンに絞る。既定（モデルの最大、Qwen2.5 は 32768）だと 7B では作業領域だけで数 GB になり、
		// メモリが足りずに止められた（周 29）。モデルはファイルの写像（mmap）で読み、2 回目からはページキャッシュに載ったものを使う
		"-c", "8192", "-lm", "mmap",
		// 長く書くと同じ行を延々と繰り返すことがある（周 30 のエディタ）。少しだけ抑える
		"--repeat-penalty", "1.05",
		"-st", "--simple-io", "--temp", temp, "--seed", fmt.Sprint(seed), "-n", fmt.Sprint(maxTokens),
		"-t", fmt.Sprint(runtime.NumCPU()), "--no-display-prompt")
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
		if strings.Contains(answer, "```c") {
			return "", nil, fmt.Errorf("the answer was cut off before the end of the ```c block; write a shorter program without repeating lines")
		}
		return "", nil, fmt.Errorf("no ```c block")
	}
	t := testsRe.FindStringSubmatch(answer)
	if t == nil {
		return "", nil, fmt.Errorf("no ```tests block")
	}
	tests, err := parseTests(t[1])
	if err != nil {
		return "", nil, err
	}
	return c[1], tests, nil
}

// "引数 => 期待する出力" の行を読む
func parseTests(block string) ([]testCase, error) {
	var tests []testCase
	for _, line := range strings.Split(block, "\n") {
		line = strings.TrimSpace(line)
		if line == "" {
			continue
		}
		// => の無い行は読み飛ばす（AI は説明や、書き直しを頼んだ文面を混ぜてくることがある）
		left, right, found := strings.Cut(line, "=>")
		if !found {
			continue
		}
		var tc testCase
		left, stdin, hasStdin := strings.Cut(left, "|")
		left = strings.TrimSpace(left)
		if left != "(none)" && left != "" {
			tc.args = strings.Fields(left)
		}
		if hasStdin {
			tc.stdin = unescapeNL(strings.TrimSpace(stdin)) + "\n"
		}
		parts := strings.Split(right, "; file ")
		tc.expected = unescapeNL(strings.TrimSpace(parts[0]))
		if tc.expected == "(none)" { // 「何も出さない」を (none) と書いてくることがある
			tc.expected = ""
		}
		for _, p := range parts[1:] {
			name, content, ok := strings.Cut(p, "=")
			name = strings.TrimSpace(name)
			if !ok || name == "" || strings.Contains(name, "/") {
				continue
			}
			tc.files = append(tc.files, fileCheck{name, unescapeNL(strings.TrimSpace(content))})
		}
		tests = append(tests, tc)
	}
	if len(tests) == 0 {
		return nil, fmt.Errorf("no test cases")
	}
	return tests, nil
}

func compile(code, bin string) (string, error) {
	src := bin + ".c"
	if err := os.WriteFile(src, []byte(code), 0o644); err != nil {
		return "", err
	}
	// 警告もエラーにする。TinyCC は char をポインタとして渡すような型の誤りを警告だけで通してしまい、実行時に落ちる（周 27）
	out, err := exec.Command(ccBin, "-Wall", "-Werror", "-o", bin, src).CombinedOutput()
	return string(out), err
}

// テストは 1 件 5 秒まで。出力は前後の空白を無視して比べる（改行の有無などの揺れを吸収する）
func testLabel(r testResult) string {
	if r.independent {
		return "  (independent)"
	}
	return ""
}

func generateIndependentTests(model, request string) ([]testCase, error) {
	var lastErr error
	for attempt := 1; attempt <= 2; attempt++ {
		answer, err := generateWith(model, testerPrompt, request, "0.3", 100+attempt, 500)
		if err != nil {
			return nil, err
		}
		os.WriteFile(filepath.Join(workDir, fmt.Sprintf("tests%d.txt", attempt)), []byte(request+"\n\n=== answer ===\n"+answer), 0o644)
		t := testsRe.FindStringSubmatch(answer)
		if t == nil {
			lastErr = fmt.Errorf("the tester wrote no ```tests block")
			continue
		}
		tests, err := parseTests(t[1])
		if err == nil {
			return tests, nil
		}
		lastErr = err
	}
	return nil, fmt.Errorf("independent tests: %v", lastErr)
}

// テストは 1 件 5 秒まで、空のディレクトリで実行する。出力とファイルの中身は normalize してから比べる
func runTests(bin string, syscalls []string, tests []testCase, independent bool) []testResult {
	var results []testResult
	for _, tc := range tests {
		dir, err := os.MkdirTemp(workDir, "run-")
		if err != nil {
			results = append(results, testResult{tc: tc, independent: independent, got: "(" + err.Error() + ")"})
			continue
		}
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		cmd := exec.CommandContext(ctx, sandboxBin, append([]string{strings.Join(syscalls, ","), bin}, tc.args...)...)
		cmd.Dir = dir
		cmd.Stdin = strings.NewReader(tc.stdin)
		out, err := cmd.CombinedOutput()
		// cancel より先に調べる。cancel した後の ctx.Err() は、時間切れでなくても nil でなくなる（周 24 で踏んだ）
		timedOut := ctx.Err() == context.DeadlineExceeded
		cancel()
		got := normalize(string(out))
		if timedOut {
			got = "(timed out)"
		} else if err != nil && strings.Contains(err.Error(), "bad system call") { // SIGSYS: seccomp に止められた
			got = strings.TrimSpace(got + " (stopped: used a system call that is not allowed)")
		} else if err != nil && strings.HasPrefix(err.Error(), "signal: ") {
			// 落ちる前に何か出力していても、落ちたことを必ず伝える（周 30: ">" と出してから落ちたのに、">" としか報告していなかった）
			got = strings.TrimSpace(got + " (crashed: " + strings.TrimPrefix(err.Error(), "signal: ") + ")")
		} else if err != nil && got == "" {
			got = "(" + err.Error() + ")"
		}
		r := testResult{tc: tc, independent: independent, got: got, passed: got == normalize(tc.expected)}
		for _, f := range tc.files {
			data, err := os.ReadFile(filepath.Join(dir, f.name))
			content := normalize(string(data))
			if err != nil {
				content = "(no such file)"
			}
			if content != normalize(f.content) {
				r.passed = false
				r.fileProblems = append(r.fileProblems, fmt.Sprintf("the file %s contained [%s], but it must contain [%s]",
					f.name, escapeNL(content), escapeNL(f.content)))
			}
		}
		os.RemoveAll(dir)
		results = append(results, r)
	}
	return results
}

// minios の API の関数が使うシステムコール（lib.h の実装に合わせる）
var apiSyscalls = map[string][]string{
	"print": {"write"}, "print_u": {"write"}, "print_i": {"write"}, "print_c": {"write"}, "print_u_pad": {"write"},
	"write_all": {"write"}, "print_err": {"write"},
	"read_file": {"open", "read", "close"}, "each_dirent": {"open", "getdents64", "close"},
	"read_line": {"read"}, "write_file": {"open", "write", "close"},
	"sha256_file": {"open", "read", "close"}, "signal": {"rt_sigaction"},
}

// ソースから、プログラムが使うシステムコールを導く。crt.h の exit で exit_group は必ず使う。
// SYS_xxx を直接書いていればそれ、minios の API の関数を呼んでいれば apiSyscalls の分。
// テストはこの一覧だけを許して（seccomp）実行するので、足りなければテストが落ちる。一覧は実際の動作で裏付けられる
func syscallsUsed(code string) []string {
	seen := map[string]bool{"exit_group": true}
	for _, s := range sysRe.FindAllString(code, -1) {
		seen[strings.TrimPrefix(s, "SYS_")] = true
	}
	for _, m := range callRe.FindAllStringSubmatch(code, -1) {
		for _, sc := range apiSyscalls[m[1]] {
			seen[sc] = true
		}
	}
	var list []string
	for s := range seen {
		list = append(list, s)
	}
	sort.Strings(list)
	return list
}

func confirm(question string) bool {
	fmt.Print(question)
	line, _ := bufio.NewReader(os.Stdin).ReadString('\n')
	return strings.TrimSpace(strings.ToLower(line)) == "y"
}

func install(name, request, model, code, bin string, attempts int, syscalls []string, results []testResult, version int, history []string) error {
	vdir := versionDir(name, version)
	for _, d := range []string{installDir, recordDir, sourceDir, vdir} {
		if err := os.MkdirAll(d, 0o755); err != nil {
			return err
		}
	}
	binData, err := os.ReadFile(bin)
	if err != nil {
		return err
	}
	modelHash, err := modelSHA256(model)
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
	fmt.Fprintf(&rec, "version %d\n", version)
	for _, h := range history {
		fmt.Fprintf(&rec, "history %s\n", strings.ReplaceAll(h, "\n", " "))
	}
	fmt.Fprintf(&rec, "model %s\n", filepath.Base(model))
	fmt.Fprintf(&rec, "model-sha256 %s\n", modelHash)
	fmt.Fprintf(&rec, "source %s\n", src)
	fmt.Fprintf(&rec, "source-sha256 %s\n", sha256Hex([]byte(code)))
	fmt.Fprintf(&rec, "binary-sha256 %s\n", sha256Hex(binData))
	fmt.Fprintf(&rec, "attempts %d\n", attempts)
	fmt.Fprintf(&rec, "syscalls %s\n", strings.Join(syscalls, " "))
	for _, r := range results {
		key := "test"
		if r.independent {
			key = "independent-test"
		}
		if !r.passed { // テストと食い違ったまま、人が承認した
			key = "disagreed-" + key
		}
		fmt.Fprintf(&rec, "%s %s\n", key, r.tc)
	}
	fmt.Fprintf(&rec, "installed %s\n", time.Now().UTC().Format(time.RFC3339))
	// 版ごとに残す（grow -undo で戻せるように）。それから今の版として置く
	files := []struct {
		path string
		data []byte
		mode os.FileMode
	}{
		{filepath.Join(vdir, "source.c"), []byte(code), 0o644},
		{filepath.Join(vdir, "binary"), binData, 0o755},
		{filepath.Join(vdir, "record"), []byte(rec.String()), 0o644},
		{src, []byte(code), 0o644},
		{filepath.Join(recordDir, name), []byte(rec.String()), 0o644},
		{filepath.Join(installDir, name), binData, 0o755},
	}
	for _, f := range files {
		if err := writeFileSync(f.path, f.data, f.mode); err != nil {
			return err
		}
	}
	return nil
}

// ---- 育てる（今の版を読む、版の履歴、元に戻す） ----

type current struct {
	version int
	history []string // これまでの依頼（最初の依頼から順に）
	source  string
}

func versionDir(name string, version int) string {
	return filepath.Join(recordDir, "versions", name, fmt.Sprint(version))
}

func readRecord(path string) (map[string][]string, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	rec := map[string][]string{}
	for _, line := range strings.Split(string(b), "\n") {
		if k, v, ok := strings.Cut(line, " "); ok {
			rec[k] = append(rec[k], v)
		}
	}
	return rec, nil
}

// /data/bin/<name> が grow で入れたものなら、今の版を返す。無ければ nil（新しく作る）
func loadCurrent(name string) (*current, error) {
	rec, err := readRecord(filepath.Join(recordDir, name))
	if os.IsNotExist(err) {
		if _, err := os.Stat(filepath.Join(installDir, name)); err == nil {
			return nil, fmt.Errorf("%s/%s exists but has no grow record (trusted by hand); it cannot be grown", installDir, name)
		}
		return nil, nil
	}
	if err != nil {
		return nil, err
	}
	if len(rec["source"]) == 0 {
		return nil, fmt.Errorf("%s/%s has no source to grow from (trusted by hand)", installDir, name)
	}
	src, err := os.ReadFile(rec["source"][0])
	if err != nil {
		return nil, err
	}
	cur := &current{version: 1, source: string(src), history: rec["history"]}
	if v := rec["version"]; len(v) > 0 {
		fmt.Sscan(v[0], &cur.version)
	}
	if len(cur.history) == 0 { // 版を記録する前（周 27 まで）に入れたもの
		cur.history = rec["request"]
	}
	return cur, nil
}

func changePrompt(name string, cur *current, request string) string {
	return fmt.Sprintf("Here is the current program named %s. What it does so far: %s.\n```c\n%s```\n\n"+
		"Change it: %s\nAnswer with the full new program and 3 tests for the new behavior, in the same format.",
		name, strings.Join(cur.history, ". "), cur.source, request)
}

func growLog(name string) int {
	rec, err := readRecord(filepath.Join(recordDir, name))
	if err != nil {
		fmt.Fprintf(os.Stderr, "grow: %s was not made with grow\n", name)
		return 1
	}
	version := 1
	if v := rec["version"]; len(v) > 0 {
		fmt.Sscan(v[0], &version)
	}
	history := rec["history"]
	if len(history) == 0 {
		history = rec["request"]
	}
	for i, h := range history {
		mark := " "
		if i+1 == version {
			mark = "*"
		}
		fmt.Printf("%s %d  %s\n", mark, i+1, h)
	}
	return 0
}

func growUndo(name string) int {
	cur, err := loadCurrent(name)
	if err != nil || cur == nil {
		fmt.Fprintf(os.Stderr, "grow: %s was not made with grow\n", name)
		return 1
	}
	if cur.version <= 1 {
		fmt.Fprintf(os.Stderr, "grow: %s is at version 1; there is nothing to go back to\n", name)
		return 1
	}
	prev := versionDir(name, cur.version-1)
	for _, f := range []struct {
		from, to string
		mode     os.FileMode
	}{
		{filepath.Join(prev, "source.c"), filepath.Join(sourceDir, name+".c"), 0o644},
		{filepath.Join(prev, "record"), filepath.Join(recordDir, name), 0o644},
		{filepath.Join(prev, "binary"), filepath.Join(installDir, name), 0o755},
	} {
		data, err := os.ReadFile(f.from)
		if err != nil {
			fmt.Fprintln(os.Stderr, "grow:", err)
			return 1
		}
		if err := writeFileSync(f.to, data, f.mode); err != nil {
			fmt.Fprintln(os.Stderr, "grow:", err)
			return 1
		}
	}
	os.RemoveAll(versionDir(name, cur.version))
	fmt.Printf("%s is back at version %d\n", name, cur.version-1)
	return 0
}

// 行ごとの差分（最長共通部分列）。"- " は消えた行、"+ " は増えた行
func lineDiff(before, after string) string {
	a := strings.Split(strings.TrimRight(before, "\n"), "\n")
	b := strings.Split(strings.TrimRight(after, "\n"), "\n")
	lcs := make([][]int, len(a)+1)
	for i := range lcs {
		lcs[i] = make([]int, len(b)+1)
	}
	for i := len(a) - 1; i >= 0; i-- {
		for j := len(b) - 1; j >= 0; j-- {
			if a[i] == b[j] {
				lcs[i][j] = lcs[i+1][j+1] + 1
			} else {
				lcs[i][j] = max(lcs[i+1][j], lcs[i][j+1])
			}
		}
	}
	var out strings.Builder
	i, j := 0, 0
	for i < len(a) || j < len(b) {
		switch {
		case i < len(a) && j < len(b) && a[i] == b[j]:
			out.WriteString("    " + a[i] + "\n")
			i, j = i+1, j+1
		case j < len(b) && (i == len(a) || lcs[i][j+1] >= lcs[i+1][j]):
			out.WriteString("  + " + b[j] + "\n")
			j++
		default:
			out.WriteString("  - " + a[i] + "\n")
			i++
		}
	}
	return out.String()
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

// モデルのハッシュ。分けて置いたモデルは全部のファイルを順に通す。7B は 4.7GB あり毎回読むと遅いので、
// ファイルの名前・大きさ・更新時刻が同じなら /tmp/grow にキャッシュした値を使う
func modelSHA256(model string) (string, error) {
	parts := []string{model}
	if m := splitRe.FindStringSubmatch(model); m != nil {
		parts, _ = filepath.Glob(m[1] + "-*-of-" + m[2] + ".gguf")
		sort.Strings(parts)
	}
	var key strings.Builder
	for _, p := range parts {
		st, err := os.Stat(p)
		if err != nil {
			return "", err
		}
		fmt.Fprintf(&key, "%s %d %d\n", p, st.Size(), st.ModTime().UnixNano())
	}
	cache := filepath.Join(workDir, "model-sha256-"+sha256Hex([]byte(key.String()))[:16])
	if b, err := os.ReadFile(cache); err == nil {
		return string(b), nil
	}
	h := sha256.New()
	for _, p := range parts {
		f, err := os.Open(p)
		if err != nil {
			return "", err
		}
		_, err = io.Copy(h, f)
		f.Close()
		if err != nil {
			return "", err
		}
	}
	sum := hex.EncodeToString(h.Sum(nil))
	os.WriteFile(cache, []byte(sum), 0o644)
	return sum, nil
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
