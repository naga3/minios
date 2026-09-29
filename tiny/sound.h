// 音（ソフト音源）の部品どうしの宣言。ゲームからは tiny.h ではなくこれを読む
#pragma once

enum { SOUND_RATE = 48000, SOUND_FPS = 60 };   // 波形の速さ、曲を進める回数（1 秒あたり）

// ファミコンの音源（apu.c）
void apu_reset(void);
void apu_write(int addr, int v);
void apu_render(short *buf, int n);
void apu_smooth(int on);         // 音量の変化を数ミリ秒かけてなめらかにする（本物には無い。既定は切る）
int apu_smoothing(void);

// MML の演奏（mml.c）。曲は 4 つの文字列（矩形波 1、矩形波 2、三角波、ノイズ）と、音量の動き（エンベロープ）の一覧
typedef struct {
    const char *track[4];
    const char *envelope[8];
} Song;
void mml_play(const Song *song);
void mml_stop(void);
int mml_playing(void);          // まだ鳴っている（L で繰り返す曲は止めるまで）
void mml_frame(void);           // 1/60 秒ごとに呼ぶ（sound.c が音を作る途中から）

// 割り込みを止める・元に戻す（止める前の状態に戻すので、割り込みの中から呼んでもよい）
#ifdef HOST
static inline unsigned long irq_off(void) { return 0; }
static inline void irq_restore(unsigned long f) { (void)f; }
#else
static inline unsigned long irq_off(void) {
    unsigned long f;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(f) :: "memory");
    return f;
}
static inline void irq_restore(unsigned long f) { __asm__ volatile("push %0; popfq" :: "r"(f) : "memory", "cc"); }
#endif
