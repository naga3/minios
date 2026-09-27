// grep <文字列>: 標準入力の各行のうち、文字列を含む行を出す（正規表現は無し）。1 行も無ければ終了コード 1
#include "../crt.h"

static int contains(const char *s, size_t n, const char *pat) {
    size_t m = slen(pat);
    for (size_t i = 0; i + m <= n; i++) {
        size_t j = 0;
        while (j < m && s[i + j] == pat[j]) j++;
        if (j == m) return 1;
    }
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) {
        print_err("usage: grep <string>", -22);
        return 2;
    }
    static char line[4096];
    size_t n = 0;
    int found = 0;
    char buf[4096];
    long r;
    while ((r = syscall3(SYS_read, 0, buf, sizeof buf)) > 0) {
        for (long i = 0; i < r; i++) {
            if (n < sizeof line) line[n++] = buf[i];
            if (buf[i] != '\n') continue;
            if (contains(line, n, argv[1])) { write_all(1, line, n); found = 1; }
            n = 0;
        }
    }
    if (n && contains(line, n, argv[1])) { write_all(1, line, n); write_all(1, "\n", 1); found = 1; }
    return found ? 0 : 1;
}
