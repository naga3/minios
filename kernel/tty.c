// 端末（シリアル）: 1 行単位の入力（エコー、バックスペース、Enter で確定、Ctrl+D で終わり）と、termios の取得・設定
#include "kernel.h"

// Linux の struct termios（ioctl の TCGETS・TCSETS で使うもの）
struct termios {
    u32 iflag, oflag, cflag, lflag;
    u8 line, cc[19];
};

#define ISIG   0000001
#define ICANON 0000002
#define ECHO   0000010
#define ICRNL  0000400
#define VINTR  0
#define VQUIT  1
#define VERASE 2
#define VEOF   4
#define VSUSP  10

static struct termios tio = {
    .iflag = ICRNL, .oflag = 05 /* OPOST | ONLCR */, .cflag = 0277 /* B38400 | CS8 | CREAD */,
    .lflag = ISIG | ICANON | ECHO | 0000020 /* ECHOE */ | 0001000 /* ECHOCTL */ | 0100000 /* IEXTEN */,
    .cc = {[VINTR] = 3, [VQUIT] = 28, [VERASE] = 127, 3 /* VKILL は使わない */, [VEOF] = 4, [VSUSP] = 26},
};

static char line[4096];       // 組み立て中の行
static u64 line_len;
static char ready[8192];      // 確定して読まれるのを待っている入力（輪）
static u64 ready_head, ready_tail;
static int eof_pending;       // Ctrl+D が押された（空の行なら読み手に 0 を返す）
long tty_fg_pgrp;             // 前面のプロセスグループ（Ctrl+C の SIGINT を送る相手）

static void echo(char c) {
    if (!(tio.lflag & ECHO)) return;
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
}

static void deliver(const char *s, u64 n) {
    for (u64 i = 0; i < n && ready_head - ready_tail < sizeof ready; i++) ready[ready_head++ % sizeof ready] = s[i];
    wakeup(&tio);
}

// シリアルの受信割り込みから 1 文字ずつ呼ばれる
void tty_input(char c) {
    if (c == '\r' && (tio.iflag & ICRNL)) c = '\n';
    if ((tio.lflag & ISIG) && (c == tio.cc[VINTR] || c == tio.cc[VQUIT])) {
        if (tio.lflag & ECHO) { serial_putc('^'); serial_putc(c + '@'); }
        line_len = 0;
        if (tty_fg_pgrp) signal_pgrp(tty_fg_pgrp, c == tio.cc[VINTR] ? 2 : 3);  // SIGINT・SIGQUIT
        return;
    }
    if (!(tio.lflag & ICANON)) {  // 1 文字ずつそのまま渡す
        echo(c);
        deliver(&c, 1);
        return;
    }
    if (c == tio.cc[VERASE] || c == '\b') {
        if (line_len > 0) {
            line_len--;
            if (tio.lflag & ECHO) { serial_putc('\b'); serial_putc(' '); serial_putc('\b'); }
        }
        return;
    }
    if (c == tio.cc[VEOF]) {  // Ctrl+D: 今の行をそのまま渡す（空なら入力の終わり）
        if (line_len == 0) eof_pending = 1;
        deliver(line, line_len);
        line_len = 0;
        wakeup(&tio);
        return;
    }
    if (line_len < sizeof line - 1) line[line_len++] = c;
    echo(c);
    if (c == '\n') {
        deliver(line, line_len);
        line_len = 0;
    }
}

long tty_read(u64 buf, u64 n) {
    while (ready_head == ready_tail && !eof_pending) {
        // 背景のプロセスグループが端末を読むと止まる（SIGTTIN）ところは省略し、前面になるまで待つ
        sleep_on(&tio);
        if (signal_pending(cur)) return -4;  // EINTR
    }
    if (ready_head == ready_tail) {
        eof_pending = 0;
        return 0;
    }
    u64 i = 0;
    while (i < n && ready_tail != ready_head) {
        char c = ready[ready_tail++ % sizeof ready];
        if (copy_out(buf + i, &c, 1) < 0) return -14;
        i++;
        if (c == '\n' && (tio.lflag & ICANON)) break;  // 1 行単位なら 1 行まで
    }
    return i;
}

long tty_write(u64 buf, u64 n) {
    for (u64 i = 0; i < n; i++) {
        char c;
        if (copy_in(&c, buf + i, 1) < 0) return -14;
        if (c == '\n' && (tio.oflag & 05) == 05) serial_putc('\r');
        serial_putc(c);
    }
    return n;
}

#define TCGETS     0x5401
#define TCSETS     0x5402
#define TCSETSW    0x5403
#define TCSBRK     0x5409
#define TIOCSCTTY  0x540E
#define TIOCGPGRP  0x540F
#define TIOCSPGRP  0x5410
#define TIOCGWINSZ 0x5413

long tty_ioctl(long cmd, u64 arg) {
    switch (cmd) {
    case TCGETS: return copy_out(arg, &tio, sizeof tio);
    case TCSETS:
    case TCSETSW: return copy_in(&tio, arg, sizeof tio);
    case TCSBRK: return 0;  // シリアルは書いた時点で送り終えている
    case TIOCSCTTY:
        cur->ctty = 1;
        if (!tty_fg_pgrp) tty_fg_pgrp = cur->pgid;
        return 0;
    case TIOCGPGRP: return copy_out(arg, &tty_fg_pgrp, 4);
    case TIOCSPGRP: {
        int pg;
        if (copy_in(&pg, arg, 4) < 0) return -14;
        tty_fg_pgrp = pg;
        return 0;
    }
    case TIOCGWINSZ: {
        u16 ws[4] = {24, 80, 0, 0};
        return copy_out(arg, ws, sizeof ws);
    }
    }
    return -25;  // ENOTTY
}
