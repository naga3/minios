// tiny の OS の関数（アセンブリ）を C から使うための宣言。
// ゲームは C で書き、画面・キーボード・タイマーは OS（アセンブリ）の関数を呼ぶ
#pragma once

enum { SCREEN_W = 320, SCREEN_H = 200 };

// 画面: 裏画面に描いて、present で写す。色は 256 色のパレットの番号
void fill_rect(long x, long y, long w, long h, int color);
void present(void);
void wait_vsync(void);   // 帰線を待つ（この後に present すれば、ちらつかない）

// スプライト: 16 × 16 の絵（1 点 1 文字、256 文字）。flip のビット 0 で左右反転、ビット 1 で上下反転。はみ出す分は切る
void draw_sprite(long x, long y, const char *pic, int flip);

// 文字: 40 文字 × 25 行
void text_color(int fg, int bg);
void text_goto(int col, int row);
void text_write(const char *s);   // 書くだけ（写さない）
void text_puts(const char *s);    // 書いて写す
void text_cursor(int on);
void con_clear(void);             // 背景の色で消して、左上へ

// キーボード: keys[スキャンコード] が 1 なら押している（矢印は 0x80 を足す）。getc_nowait は貯まった文字（無ければ 0）
extern volatile unsigned char keys[256];
unsigned char getc_nowait(void);
enum { KEY_ESC = 27, KEY_UP = 0x80, KEY_DOWN = 0x81, KEY_LEFT = 0x82, KEY_RIGHT = 0x83 };
enum { SC_W = 0x11, SC_A = 0x1e, SC_S = 0x1f, SC_D = 0x20, SC_Z = 0x2c, SC_X = 0x2d, SC_SPACE = 0x39,
       SC_LSHIFT = 0x2a, SC_RSHIFT = 0x36, SC_UP = 0xc8, SC_LEFT = 0xcb, SC_RIGHT = 0xcd, SC_DOWN = 0xd0 };

// タイマー: ticks は起動してからの回数（1 回 = 10ms）
extern volatile unsigned long ticks;
void sleep_ticks(unsigned long n);
#ifdef HOST                         // PC の上で試すとき（tools/）は、休む代わりに試しの道具の関数を呼ぶ
void halt_hook(void);
static inline void halt(void) { halt_hook(); }
#else
static inline void halt(void) { __asm__ volatile("hlt"); }   // 次の割り込みまで CPU を休める
#endif

// 256 色のパレット: 0〜15 は DOS の 16 色、16 + 赤 × 36 + 緑 × 6 + 青（それぞれ 0〜5）は色の立方体
#define RGB6(r, g, b) (16 + (r) * 36 + (g) * 6 + (b))
