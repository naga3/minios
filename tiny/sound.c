// 音: AC97（ac97.s）が、バッファを 1 つ鳴らし終えるたびに割り込みの中から sound_fill を呼ぶ。
// ここで 48,000 Hz・16 ビット・モノラルの波形を作る（左右に写すのは ac97.s）。
// 波形はファミコンの音源（apu.c）が作り、1/60 秒（800 サンプル）ごとに MML の演奏（mml.c）を進める
#include "tiny.h"
#include "sound.h"

extern unsigned char sound_ok;

void sound_fill(short *buf, int n) {
    static int ready, frame_left;
    if (!ready) {                               // 最初の 1 回（起動時、sound_init から）で音源を初めの状態に
        apu_reset();
        ready = 1;
    }
    for (int i = 0; i < n; i++) buf[i] = 0;
    while (n > 0) {                             // 1/60 秒の区切りまで作っては、曲を進める
        if (frame_left == 0) {
            mml_frame();
            frame_left = SOUND_RATE / SOUND_FPS;
        }
        int k = n < frame_left ? n : frame_left;
        apu_render(buf, k);
        buf += k;
        n -= k;
        frame_left -= k;
    }
}

// 曲が終わるまで待つ。Esc で止める
static void wait_song(void) {
    while (mml_playing()) {
        unsigned char c = getc_nowait();
        if (c == KEY_ESC) {
            mml_stop();
            break;
        }
        halt();
    }
}

// ---- 試しの曲（シェルの sound）。オリジナル ----

static const Song demo = {
    .track = {
        // 矩形波 1: メロディ（25%）。少し小さくしながら伸ばし、離したらゆっくり消える
        "t132 l8 o5 @1 @v0 q7"
        "e g >c< g a g e r f a >d< a g4 r4 e g >c< g a g e c d f e d c4 r4"
        "@2 @v1 q6 [c e g e]2 [d f a f]2 c e g >c< b g e d c2 r2",
        // 矩形波 2: ハモり（12.5%）
        "l8 o4 @0 @v0 q7 v11"
        "c4 e4 f4 c4 d4 f4 d4 r4 c4 e4 f4 c4 < b4 g4 > e4 r4"
        "@v1 q6 v10 [e4 c4]2 [f4 d4]2 e4 c4 e4 g4 e2 r2",
        // 三角波: ベース。短く歯切れよく
        "l8 o3 q5"
        "c c c c f f c c g g g g < g g > g g c c c c f f < a a g g g g > c4 r4"
        "q6 [c4 g4]2 [f4 a4]2 c4 g4 < g4 > g4 c2 r2",
        // ノイズ: ハイハットとスネア（1 は短く、2 は少し長く）
        "l8 [@v2 o5 c r @v3 o4 g r @v2 o5 c c @v3 o4 g r]3 @v2 o5 c r @v3 o4 g r @v2 o5 c r @v3 o4 g g"
        "[@v2 o5 c r @v3 o4 g r @v2 o5 c c @v3 o4 g r]4",
    },
    .envelope = {
        "15 14 13 12 12 11 11 10 | 10 / 8 6 5 4 3 2 1 1 0",   // 0: やわらかく伸ばして、ゆっくり消える
        "15 13 11 10 9 | 9 / 7 5 3 2 1 0",                     // 1: 少しはっきり
        "12 8 5 3 1 0",                                        // 2: ハイハット
        "15 12 10 8 6 5 4 3 2 1 0",                            // 3: スネア
    },
};

void sound_demo(void) {
    if (!sound_ok) {
        text_puts("no sound card\n");
        return;
    }
    text_puts("playing... (Esc: stop)\n");
    mml_play(&demo);
    wait_song();
}

// シェルの play: 打った MML を矩形波 1 で鳴らす（例: play t120 o4 cdefgab>c）
void play_command(const char *mml) {
    static char line[64];
    static Song song = { .envelope = { "15 14 13 12 | 12 / 9 6 4 2 1 0" } };
    if (!sound_ok) {
        text_puts("no sound card\n");
        return;
    }
    int i = 0;
    for (const char *p = "@v0 q7 "; *p;) line[i++] = *p++;
    while (*mml && i < (int)sizeof line - 1) line[i++] = *mml++;
    line[i] = 0;
    song.track[0] = line;
    mml_play(&song);
    wait_song();
}
