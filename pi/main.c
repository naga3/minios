// Pi 版のシェル: tiny/main.s のシェルと同じコマンド（help・clear・jump・sound・play・smooth）
#include "tiny.h"
#include "hal.h"

void jump_main(const char *arg);
void sound_demo(void);
void play_command(const char *mml);
void smooth_command(void);

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}
// line が word で始まり、その後が行の終わりか空白なら、空白を飛ばした続きを返す（違えば 0）
static const char *prefix(const char *line, const char *word) {
    while (*word) if (*line++ != *word++) return 0;
    if (*line && *line != ' ') return 0;
    while (*line == ' ') line++;
    return line;
}

static int read_line(char *buf) {
    int n = 0;
    for (;;) {
        unsigned char c = getc_wait();
        if (c == '\n') break;
        if (c == '\b') {
            if (n > 0) { n--; text_puts("\b \b"); }
        } else if (c >= 0x20 && c < 0x7f && n < 37) {    // 「> 」と合わせて 1 行（40 文字）に収める
            buf[n++] = c;
            con_putc_raw(c);
        }
    }
    buf[n] = 0;
    text_puts("\n");
    return n;
}

static const char title[] = "minios pi  320x200 256 colors\n";
static const char subtitle[] = "\nThe quick brown fox jumps over\nthe lazy dog. 0123456789 !?@#$%&*\n\nType help for commands.\n";
static const char help_text[] =
    "jump   ROBO JUMP (jump 3-2: from 3-2)\nclear  clear the screen\nsound  play a tune (NES-style sound)\n"
    "play   play MML: play t120 o4 cdefg\nsmooth smooth volume changes on/off\nhash   show the last tune's checksum\nother  echo the line\n";

int main(void) {
    hal_init();
    text_color(7, 1);
    con_clear();
    text_color(14, 1);
    text_puts(title);
    text_color(7, 1);
    for (int i = 0; i < 16; i++) fill_rect(i * 20, 8, 20, 6, i);      // 16 色の見本
    text_puts(subtitle);
    for (;;) {
        char line[40];
        const char *rest;
        text_puts("> ");
        read_line(line);
        if ((rest = prefix(line, "jump"))) jump_main(rest);
        else if (streq(line, "clear")) con_clear();
        else if (streq(line, "sound")) sound_demo();
        else if ((rest = prefix(line, "play"))) play_command(rest);
        else if (streq(line, "smooth")) smooth_command();
        else if (streq(line, "hash")) audio_report();
        else if (streq(line, "help")) text_puts(help_text);
        else if (line[0]) { text_puts(line); text_puts("\n"); }
    }
}
