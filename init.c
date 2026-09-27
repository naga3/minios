// minios の PID 1。起動処理（boot.h）を済ませ、端末ごとにシェル（shell.h）を起動して見守る
#include "crt.h"
#include "boot.h"
#include "shell.h"

// シェルを動かす端末。無いもの（画面の無いカーネルの tty1 など）は飛ばす
static const char *const terminals[] = {"/dev/ttyS0", "/dev/tty1"};
#define NTERMINALS (sizeof terminals / sizeof terminals[0])

static long now_sec(void) {
    struct { long sec, nsec; } t;
    syscall3(SYS_clock_gettime, 1 /* CLOCK_MONOTONIC */, &t, 0);
    return t.sec;
}

static long start_session(const char *tty, int first) {
    long pid = syscall3(SYS_fork, 0, 0, 0);
    if (pid == 0) session(tty, first);
    return pid;
}

// crt.h の _start がスタックを 16 バイト境界に揃えてから呼ぶ。
// init を _start から直接書くと揃っておらず、SSE 命令（movaps）で落ちる（周 18 で踏んだ）
int main(int argc, char **argv, char **envp) {
    (void)argc, (void)argv, (void)envp;
    print("\033[2J\033[H");  // 画面クリア + カーソルを左上へ
    banner();
    mount_all();
    net_up();
    // 永続ディスクの起動スクリプト。OS をビルドし直さずに起動時の振る舞いを変えられる
    struct stat st;
    if (syscall3(SYS_stat, "/data/rc", &st, 0) >= 0) {
        status("Running /data/rc", 0);
        source_file("/data/rc", 1);
    }

    // 起動メッセージを出し終えてからシェルを起動する。先に起動すると、シェルのプロンプトの後に
    // init のメッセージが割り込むことがある（シェルと init は同じシリアルに書くため）
    int present[NTERMINALS] = {0};
    for (size_t i = 0; i < NTERMINALS; i++) {
        if (syscall3(SYS_stat, terminals[i], &st, 0) < 0) continue;
        present[i] = 1;
        char what[64] = "Shell on ";
        size_t n = slen(what);
        for (const char *p = terminals[i] + 5; *p && n < sizeof what - 1; p++) what[n++] = *p;  // "/dev/" を除く
        what[n] = 0;
        status(what, 0);
    }
    long pids[NTERMINALS] = {0}, started[NTERMINALS] = {0};
    for (size_t i = 0; i < NTERMINALS; i++) {
        if (!present[i]) continue;
        pids[i] = start_session(terminals[i], 1);
        started[i] = now_sec();
    }

    // あとは子プロセスの回収だけ。親が先に終わった孤児も PID 1 の子になるので、ここで回収する。
    // シェルが終わったら起動し直す（getty と同じ）。すぐ終わるのを繰り返すなら 1 秒待つ
    for (;;) {
        int status_ = 0;
        long pid = syscall6(SYS_wait4, -1, (long)&status_, 0, 0, 0, 0);
        if (pid < 0) {
            struct { long sec, nsec; } ts = {1, 0};
            syscall3(SYS_nanosleep, &ts, 0, 0);
            continue;
        }
        for (size_t i = 0; i < NTERMINALS; i++) {
            if (pids[i] != pid) continue;
            if (now_sec() - started[i] < 1) {
                struct { long sec, nsec; } ts = {1, 0};
                syscall3(SYS_nanosleep, &ts, 0, 0);
            }
            pids[i] = start_session(terminals[i], 0);
            started[i] = now_sec();
        }
    }
}
