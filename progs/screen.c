// screen [n]: 画面の端末 tty<n>（既定 1）に表示されている文字を、行ごとに "|" を付けて出す。
// /dev/vcsa<n> は先頭 4 バイトが（行数、列数、カーソルの x、y）、その後に（文字、色）の 2 バイトが並ぶ
#include "../crt.h"

int main(int argc, char **argv, char **envp) {
    (void)envp;
    char path[32] = "/dev/vcsa";
    const char *n = argc > 1 ? argv[1] : "1";
    size_t len = slen(path);
    for (const char *p = n; *p && len < sizeof path - 1; p++) path[len++] = *p;
    path[len] = 0;

    static char buf[2 * 256 * 128 + 4];
    long r = read_file(path, buf, sizeof buf);
    if (r < 4) {
        print_err(path, r < 0 ? r : -5);
        return 1;
    }
    unsigned rows = (unsigned char)buf[0], cols = (unsigned char)buf[1];
    for (unsigned y = 0; y < rows; y++) {
        char line[257];
        unsigned end = 0;
        for (unsigned x = 0; x < cols && x < 256; x++) {
            size_t i = 4 + 2 * (y * cols + x);
            if (i >= (size_t)r) break;
            char c = buf[i];
            line[x] = (c >= 32 && c < 127) ? c : ' ';
            if (line[x] != ' ') end = x + 1;
        }
        line[end] = 0;
        print("|"); print(line); print("\n");
    }
    return 0;
}
