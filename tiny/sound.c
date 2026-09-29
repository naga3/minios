// 音: AC97（ac97.s）が、バッファを 1 つ鳴らし終えるたびに割り込みの中から sound_fill を呼ぶ。
// ここで 48,000 Hz・16 ビット・モノラルの波形を作る（左右に写すのは ac97.s）。いまはファミコンの音源（apu.c）だけを鳴らす
#include "tiny.h"

void apu_render(short *buf, int n);
void apu_write(int addr, int v);
void apu_reset(void);
extern unsigned char sound_ok;

void sound_fill(short *buf, int n) {
    static int ready;
    if (!ready) {                               // 最初の 1 回（起動時、sound_init から）で音源を初めの状態に
        apu_reset();
        ready = 1;
    }
    for (int i = 0; i < n; i++) buf[i] = 0;
    apu_render(buf, n);
}

// ---- 試しの曲（シェルの sound） ----

// 音の名前（"C5"、"F#3"）から MIDI の番号。休みは -1
static int note_number(const char **p) {
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const char *s = *p;
    while (*s == ' ') s++;
    if (*s == '.') {
        *p = s + 1;
        return -1;
    }
    int n = base[*s - 'A'];
    s++;
    if (*s == '#') n++, s++;
    n += (*s - '0' + 1) * 12;
    *p = s + 1;
    return n;
}

// MIDI の番号の周波数（1/1000 Hz）: 4 オクターブ目の表から、オクターブの分だけずらす
static long note_mhz(int n) {
    static const long c4[12] = { 261626, 277183, 293665, 311127, 329628, 349228,
                                 369994, 391995, 415305, 440000, 466164, 493883 };
    int oct = n / 12 - 5;
    long f = c4[n % 12];
    return oct >= 0 ? f << oct : f >> -oct;
}

// 矩形波の周期 = 1,789,773 ÷ (16 × 周波数) - 1。三角波は 32 で割る
static int period(int n, int div) { return (int)(1789773000LL / (div * note_mhz(n))) - 1; }

static void pulse(int ch, int n, int duty) {
    int base = 0x4000 + ch * 4, t = period(n, 16);
    apu_write(base, duty << 6 | 4);             // 音量は 15 から下がっていく（1 段 5/240 秒）
    apu_write(base + 1, 0x08);                  // スイープは使わない（引き算にしておくと低い音も消えない）
    apu_write(base + 2, t & 0xff);
    apu_write(base + 3, 1 << 3 | t >> 8);       // 長さは長め（254）
}

static void triangle(int n) {
    int t = period(n, 32);
    apu_write(0x4008, 40);                      // リニアカウンタ 40/240 秒で止まる（歯切れよく）
    apu_write(0x400a, t & 0xff);
    apu_write(0x400b, 1 << 3 | t >> 8);
}

static void drum(char d) {
    if (d == 'h') {                             // ハイハット: 高いノイズをすぐ小さく
        apu_write(0x400c, 0x01);
        apu_write(0x400e, 0x03);
    } else {                                    // スネア: 中くらいのノイズを少し長く
        apu_write(0x400c, 0x03);
        apu_write(0x400e, 0x07);
    }
    apu_write(0x400f, 1 << 3);
}

void sound_demo(void) {
    // 8 分音符ごと。オリジナルの短い曲
    static const char *melody =  "E5 G5 C6 G5 A5 G5 E5 .  F5 A5 D6 A5 G5 .  .  .  E5 G5 C6 G5 A5 G5 E5 C5 D5 F5 E5 D5 C5 .  .  . ";
    static const char *harmony = "C5 .  E5 .  F5 .  C5 .  D5 .  F5 .  D5 .  .  .  C5 .  E5 .  F5 .  C5 .  B4 .  G4 .  E4 .  .  . ";
    static const char *bass =    "C3 .  C3 .  F3 .  C3 .  G3 .  G3 .  G3 .  G2 .  C3 .  C3 .  F3 .  A2 .  G2 .  G2 .  C3 .  C3 . ";
    static const char *drums =   "h.s.h.s.h.s.hhs.h.s.h.s.h.s.s.s.";
    if (!sound_ok) {
        text_puts("no sound card\n");
        return;
    }
    text_puts("playing...\n");
    apu_reset();
    apu_write(0x4015, 0x0f);
    const char *m = melody, *h = harmony, *b = bass;
    for (int i = 0; i < 32; i++) {
        int n;
        if ((n = note_number(&m)) >= 0) pulse(0, n, 1);
        if ((n = note_number(&h)) >= 0) pulse(1, n, 2);
        if ((n = note_number(&b)) >= 0) triangle(n);
        if (drums[i] != '.') drum(drums[i]);
        sleep_ticks(15);                        // 8 分音符 = 0.15 秒（テンポ 200）
    }
    sleep_ticks(40);
    apu_write(0x4015, 0);
}
