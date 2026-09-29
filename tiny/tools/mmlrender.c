// MML の曲を PC の上で演奏し、波形を出す（48,000 Hz・16 ビット・モノラルの生データを標準出力へ）。
// sound.c・apu.c・mml.c を OS の中と同じまま使う
//   cc -DHOST -I tiny -o build/mmlrender tiny/tools/mmlrender.c tiny/sound.c tiny/apu.c tiny/mml.c
//   build/mmlrender 2000 "t120 o4 l4 cdef" ["矩形波 2" "三角波" "ノイズ"] > a.raw   （2000 ミリ秒）
//   build/mmlrender demo > a.raw   （sound の試しの曲を最後まで）
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sound.h"

// sound.c が使う OS の関数の代わり
unsigned char sound_ok = 1;
static int esc;
void text_puts(const char *s) { (void)s; }
unsigned char getc_nowait(void) { return esc; }
void sound_fill(short *buf, int n);
void sound_demo(void);

static void out(int n) {
    static short buf[4800];
    while (n > 0) {
        int k = n < 4800 ? n : 4800;
        sound_fill(buf, k);
        fwrite(buf, 2, k, stdout);
        n -= k;
    }
}

// wait_song の中の hlt の代わりに、1/100 秒ずつ音を作る。長すぎたら Esc で止める
static int halts;
void halt_hook(void) {
    out(SOUND_RATE / 100);
    if (++halts > 100 * 120) esc = 27;
}

int main(int argc, char **argv) {
    short none[1];
    sound_fill(none, 0);                        // 音源を初めの状態に（OS では起動時に sound_init が最初に呼ぶ）
    if (argc > 1 && !strcmp(argv[1], "demo")) {
        sound_demo();
        return 0;
    }
    if (argc < 3) return fprintf(stderr, "usage: mmlrender ms track1 [track2 track3 track4]\n"), 1;
    static Song song = { .envelope = { "15 14 13 12 | 12 / 9 6 4 2 1 0", "15 13 11 10 9 | 9 / 7 5 3 2 1 0", "12 8 5 3 1 0",
                                       "15 12 10 8 6 5 4 3 2 1 0" } };
    for (int i = 0; i < 4 && i + 2 < argc; i++) song.track[i] = argv[i + 2];
    mml_play(&song);
    out(atoi(argv[1]) * SOUND_RATE / 1000);
    return 0;
}
