// Pi 3（aarch64）の土台: tiny.h の関数（画面・文字・キー・時計・音のペース）を、割り込みなしのポーリングで実装する。
// ゲーム（robo.c など）と音源（apu.c・mml.c）は、tiny/ のものを -DHOST で作ってそのまま使う
// （-DHOST は halt を halt_hook に、irq_off を何もしないものに替える。PC の上で試す仕組みを、割り込みなしの Pi に使い回している）
#include "tiny.h"
#include "sound.h"
#include "hal.h"
#include "font.h"

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long u64;

// ---- 装置の場所 ----
#define MMIO 0x3F000000UL
#define UART_DR (*(volatile u32 *)(MMIO + 0x201000))
#define UART_FR (*(volatile u32 *)(MMIO + 0x201018))
#define MBOX_READ   (*(volatile u32 *)(MMIO + 0xB880))
#define MBOX_STATUS (*(volatile u32 *)(MMIO + 0xB898))
#define MBOX_WRITE  (*(volatile u32 *)(MMIO + 0xB8A0))

// ---- 小さな関数（コンパイラが呼ぶ）----
void *memcpy(void *to, const void *from, unsigned long n) {
    u8 *d = to; const u8 *s = from;
    while (n--) *d++ = *s++;
    return to;
}
void *memmove(void *to, const void *from, unsigned long n) {
    u8 *d = to; const u8 *s = from;
    if (d < s) while (n--) *d++ = *s++;
    else { d += n; s += n; while (n--) *--d = *--s; }
    return to;
}
void *memset(void *to, int c, unsigned long n) {
    u8 *d = to;
    while (n--) *d++ = (u8)c;
    return to;
}

// ---- UART（シリアル。QEMU では端末とつながる）----
void uart_putc(char c) {
    if (c == '\n') uart_putc('\r');
    while (UART_FR & 0x20) {}
    UART_DR = c;
}
void uart_puts(const char *s) { while (*s) uart_putc(*s++); }
void uart_puthex(u64 v) { uart_puts("0x"); for (int i = 60; i >= 0; i -= 4) uart_putc("0123456789abcdef"[(v >> i) & 15]); }
void uart_putdec(u64 v) {
    char b[24]; int i = 23; b[i] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v);
    uart_puts(b + i);
}

// ---- 時計（ARM のカウンタ）----
static inline u64 counter(void) { u64 v; __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v)); return v; }
static u64 freq, t0;
volatile unsigned long ticks;

// ---- 画面: 裏画面（256 色の番号）に描き、present で 32 ビットの画面へ変換して写す ----
static u8 backbuf[SCREEN_W * SCREEN_H];
static volatile u32 *fb;
// 本物の画面は 640 × 480。QEMU が最初に用意する大きさと同じにして、ゲストが大きさを変えないようにする
// （変えると、QEMU の raspi3b が表示を作り直す途中に落ちることがある）。ゲームの画面は 2 倍にして、上下に帯を付けて真ん中に出す
enum { FB_W = 640, FB_H = 480, SCALE = 2, X_OFF = (FB_W - SCREEN_W * SCALE) / 2, Y_OFF = (FB_H - SCREEN_H * SCALE) / 2 };
static u32 pal[256];
static volatile u32 mb[36] __attribute__((aligned(16)));

static int mbox_call(void) {
    u32 msg = ((u32)(u64)mb & ~0xFu) | 8;
    while (MBOX_STATUS & 0x80000000) {}
    MBOX_WRITE = msg;
    for (;;) {
        while (MBOX_STATUS & 0x40000000) {}
        if (MBOX_READ == msg) return mb[1] == 0x80000000;
    }
}

// VGA の 6 ビット（0〜63）の色を 8 ビットに
static u32 c8(u32 v) { return v * 255 / 63; }
static u32 rgb(u32 r6, u32 g6, u32 b6) { return 0xFF000000 | c8(b6) << 16 | c8(g6) << 8 | c8(r6); }   // Pi の画面は B・G・R の並び

static void palette_init(void) {
    static const u8 dos[16][3] = {
        {0, 0, 0}, {0, 0, 42}, {0, 42, 0}, {0, 42, 42}, {42, 0, 0}, {42, 0, 42}, {42, 21, 0}, {42, 42, 42},
        {21, 21, 21}, {21, 21, 63}, {21, 63, 21}, {21, 63, 63}, {63, 21, 21}, {63, 21, 63}, {63, 63, 21}, {63, 63, 63} };
    static const u8 cube[6] = {0, 13, 25, 38, 50, 63};
    for (int i = 0; i < 16; i++) pal[i] = rgb(dos[i][0], dos[i][1], dos[i][2]);
    for (int i = 0; i < 216; i++) pal[16 + i] = rgb(cube[i / 36], cube[i / 6 % 6], cube[i % 6]);
    for (int i = 0; i < 24; i++) pal[232 + i] = rgb(i * 63 / 23, i * 63 / 23, i * 63 / 23);
}

void present(void) {
    for (int y = 0; y < SCREEN_H; y++) {
        volatile u32 *row = fb + (Y_OFF + y * SCALE) * FB_W + X_OFF;
        for (int x = 0; x < SCREEN_W; x++) {
            u32 c = pal[backbuf[y * SCREEN_W + x]];
            row[x * SCALE] = row[x * SCALE + 1] = row[FB_W + x * SCALE] = row[FB_W + x * SCALE + 1] = c;
        }
    }
}

void fill_rect(long x, long y, long w, long h, int color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;
    for (long j = 0; j < h; j++) {
        u8 *p = backbuf + (y + j) * SCREEN_W + x;
        for (long i = 0; i < w; i++) p[i] = (u8)color;
    }
}

// ---- スプライト: 文字 → 色 の表（sprite.s と同じ）----
static u8 char_colors[128];
static void sprite_init(void) {
    static const struct { char c; u8 v; } extra[] = {
        {'o', 16 + 5 * 36 + 3 * 6 + 0}, {'r', 16 + 4 * 36 + 1 * 6 + 0}, {'n', 16 + 4 * 36 + 2 * 6 + 1}, {'k', 16 + 3 * 36 + 1 * 6 + 0},
        {'s', 16 + 2 * 36 + 4 * 6 + 5}, {'h', 16 + 1 * 36 + 1 * 6 + 2}, {'z', 16 + 1 * 36 + 0 * 6 + 0}, {'j', 16 + 1 * 36 + 0 * 6 + 2},
        {'v', 16 + 0 * 36 + 2 * 6 + 1}, {'q', 16 + 3 * 36 + 0 * 6 + 1}, {'p', 16 + 5 * 36 + 3 * 6 + 4}, {'l', 16 + 3 * 36 + 4 * 6 + 5},
        {'m', 16 + 1 * 36 + 2 * 6 + 5}, {'u', 16 + 0 * 36 + 1 * 6 + 3}, {'x', 16 + 5 * 36 + 4 * 6 + 3}, {'y', 16 + 5 * 36 + 5 * 6 + 2},
        {'w', 16 + 5 * 36 + 5 * 6 + 4}, {'g', 16 + 4 * 36 + 4 * 6 + 4}, {'t', 16 + 3 * 36 + 5 * 6 + 2}, {'i', 16 + 1 * 36 + 4 * 6 + 1} };
    for (int i = 0; i < 128; i++) char_colors[i] = 0xff;                 // 0xff は透明
    for (int i = 0; i < 10; i++) char_colors['0' + i] = i;
    for (int i = 0; i < 6; i++) char_colors['a' + i] = 10 + i;
    for (unsigned i = 0; i < sizeof extra / sizeof extra[0]; i++) char_colors[(int)extra[i].c] = extra[i].v;
}

void draw_sprite(long x, long y, const char *pic, int flip) {
    for (int r = 0; r < 16; r++) {
        long py = y + r;
        if (py < 0 || py >= SCREEN_H) continue;
        int sr = (flip & 2) ? 15 - r : r;
        for (int c = 0; c < 16; c++) {
            long px = x + c;
            if (px < 0 || px >= SCREEN_W) continue;
            int sc = (flip & 1) ? 15 - c : c;
            u8 col = char_colors[pic[sr * 16 + sc] & 0x7f];
            if (col != 0xff) backbuf[py * SCREEN_W + px] = col;
        }
    }
}

// ---- 文字: 8 × 8 のフォントで 40 文字 × 25 行（console.s と同じ）----
enum { COLS = 40, ROWS = 25 };
static int con_row, con_col, con_fg = 7, con_bg, con_cursor_on = 1;

static void cursor_draw(int color) {
    if (con_cursor_on) fill_rect(con_col * 8, con_row * 8 + 7, 8, 1, color);
}
static void draw_char(int ch, int x, int y) {
    int i = ch - 0x20;
    if (i < 0 || i >= 95) i = '?' - 0x20;
    for (int r = 0; r < 8; r++) {
        u8 bits = font[i * 8 + r];
        for (int c = 0; c < 8; c++) backbuf[(y + r) * SCREEN_W + x + c] = (bits & (0x80 >> c)) ? con_fg : con_bg;
    }
}
static void scroll(void) {
    memmove(backbuf, backbuf + SCREEN_W * 8, SCREEN_W * 8 * (ROWS - 1));
    fill_rect(0, 8 * (ROWS - 1), SCREEN_W, 8, con_bg);
}
static void putc_nopresent(char ch) {
    cursor_draw(con_bg);
    if (ch == '\b') {
        if (con_col > 0) con_col--;
    } else if (ch == '\r') {
        con_col = 0;
    } else {
        if (ch != '\n') {
            draw_char((u8)ch, con_col * 8, con_row * 8);
            con_col++;
        }
        if (ch == '\n' || con_col >= COLS) {
            con_col = 0;
            con_row++;
            if (con_row >= ROWS) { scroll(); con_row = ROWS - 1; }
        }
    }
    cursor_draw(con_fg);
}
void text_color(int fg, int bg) { con_fg = fg; con_bg = bg; }
void text_goto(int col, int row) { cursor_draw(con_bg); con_col = col; con_row = row; cursor_draw(con_fg); }
void text_cursor(int on) { cursor_draw(con_bg); con_cursor_on = on; cursor_draw(con_fg); }
// シェル（カーソルあり）のときだけ UART にも写す。ゲームは毎コマ文字を書くので、写すと端末があふれる
void text_write(const char *s) { while (*s) { if (con_cursor_on) uart_putc(*s); putc_nopresent(*s++); } }
void text_puts(const char *s) { text_write(s); present(); }
void con_putc_raw(char c) { if (con_cursor_on) uart_putc(c); putc_nopresent(c); present(); }
void con_clear(void) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, con_bg);
    con_row = con_col = 0;
    cursor_draw(con_fg);
    present();
}

// ---- キーボード: UART に届いた文字を、キーを押した・押し続けている ことにする（QEMU で試すための代わり。実機ではボタン）----
volatile unsigned char keys[256];
static u8 queue[64];
static int q_head, q_tail;
static unsigned long release_at[256];
enum { HOLD = 12 };                      // 文字が届いてから、押している扱いにする間（1/100 秒）。端末のキーリピートの間隔に合わせた

static void enqueue(u8 c) {
    int n = (q_head + 1) % 64;
    if (n != q_tail) { queue[q_head] = c; q_head = n; }
}
static void press(int sc, int ch) {
    if (sc) { keys[sc] = 1; release_at[sc] = ticks + HOLD; }
    if (ch) enqueue(ch);
}
static int esc_state;                    // 0: 通常、1: ESC を受け取った、2: ESC [ を受け取った
static unsigned long esc_at;

static void key_char(u8 c) {
    if (esc_state == 1) {
        if (c == '[') { esc_state = 2; return; }
        enqueue(KEY_ESC);                // 単独の ESC だった
        esc_state = 0;
    } else if (esc_state == 2) {
        esc_state = 0;
        if (c == 'A') press(SC_UP, KEY_UP);
        else if (c == 'B') press(SC_DOWN, KEY_DOWN);
        else if (c == 'C') press(SC_RIGHT, KEY_RIGHT);
        else if (c == 'D') press(SC_LEFT, KEY_LEFT);
        return;
    }
    if (c == 27) { esc_state = 1; esc_at = ticks; return; }
    if (c == '\r') c = '\n';
    if (c == 0x7f) c = '\b';
    int sc = 0;
    switch (c) {
    case 'w': sc = SC_W; break;
    case 'a': sc = SC_A; break;
    case 's': sc = SC_S; break;
    case 'd': sc = SC_D; break;
    case 'z': sc = SC_Z; break;
    case 'x': sc = SC_X; break;
    case ' ': sc = SC_SPACE; break;
    }
    press(sc, c);
}

unsigned char getc_nowait(void) {
    if (q_head == q_tail) return 0;
    u8 c = queue[q_tail];
    q_tail = (q_tail + 1) % 64;
    return c;
}

// ---- 音: 時間に合わせてサンプルを作る（出口の PWM と DMA は未実装。いまは作った波形のハッシュを取るだけ）----
unsigned char sound_ok = 1;
void sound_fill(short *buf, int n);
enum { CHUNK = SOUND_RATE / 100 };       // 10 ミリ秒（PC の mmlrender の halt_hook と同じ）
static int audio_active;
static u64 audio_t0, audio_made, audio_hash = 0xcbf29ce484222325UL;
static u64 last_made, last_hash;

static void audio_pump(void) {
    if (!mml_playing()) {
        if (audio_active) { audio_active = 0; last_made = audio_made; last_hash = audio_hash; }
        return;
    }
    if (!audio_active) {
        audio_active = 1;
        audio_t0 = counter();
        audio_made = 0;
        audio_hash = 0xcbf29ce484222325UL;
    }
    u64 target = (counter() - audio_t0) * SOUND_RATE / freq;
    while (mml_playing() && audio_made < target) {
        static short buf[CHUNK];
        sound_fill(buf, CHUNK);
        for (int i = 0; i < CHUNK; i++) {                // 16 ビットを下位・上位の順に FNV-1a へ（PC の s16le の生データと同じ並び）
            audio_hash = (audio_hash ^ (u8)(buf[i] & 0xff)) * 0x100000001b3UL;
            audio_hash = (audio_hash ^ (u8)((buf[i] >> 8) & 0xff)) * 0x100000001b3UL;
        }
        audio_made += CHUNK;
    }
    if (!mml_playing() && audio_active) { audio_active = 0; last_made = audio_made; last_hash = audio_hash; }
}

void audio_report(void) {
    uart_puts("samples "); uart_putdec(last_made); uart_puts(" fnv1a "); uart_puthex(last_hash); uart_puts("\n");
}

// ---- ポーリング: halt の代わりに呼ばれる（待つ間・1 コマごと）----
static void poll(void) {
    ticks = (counter() - t0) * 100 / freq;
    while (!(UART_FR & 0x10)) key_char(UART_DR & 0xff);
    if (esc_state == 1 && ticks > esc_at + 5) { enqueue(KEY_ESC); esc_state = 0; }
    for (int i = 0; i < 256; i++)
        if (keys[i] && ticks >= release_at[i]) keys[i] = 0;
    audio_pump();
}
void halt_hook(void) { poll(); }

unsigned char getc_wait(void) {
    unsigned char c;
    while (!(c = getc_nowait())) poll();
    return c;
}

void sleep_ticks(unsigned long n) {
    unsigned long end = ticks + n;
    while (ticks < end) poll();
}

// 60 Hz のペース（実機の垂直帰線の代わり）。待つ間も入力と音を進める
void wait_vsync(void) {
    static u64 next;
    u64 step = freq / 60, now = counter();
    if (next == 0 || now > next + step * 4) next = now;
    next += step;
    while (counter() < next) poll();
    poll();
}

void hal_init(void) {
    freq = ({ u64 f; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f)); f; });
    if (freq == 0) freq = 19200000;      // 実機の Pi 3 は、ファームウェアが設定しないことがある（水晶は 19.2 MHz）
    t0 = counter();
    int i = 0;
    mb[i++] = 35 * 4; mb[i++] = 0;
    mb[i++] = 0x48003; mb[i++] = 8; mb[i++] = 0; mb[i++] = FB_W; mb[i++] = FB_H;
    mb[i++] = 0x48004; mb[i++] = 8; mb[i++] = 0; mb[i++] = FB_W; mb[i++] = FB_H;
    mb[i++] = 0x48005; mb[i++] = 4; mb[i++] = 0; mb[i++] = 32;
    mb[i++] = 0x40001; mb[i++] = 8; mb[i++] = 0; mb[i++] = 16; mb[i++] = 0;
    mb[i++] = 0;
    if (!mbox_call()) { uart_puts("mailbox failed\n"); for (;;) {} }
    fb = (volatile u32 *)(u64)(mb[19] & 0x3FFFFFFF);
    for (int i = 0; i < FB_W * FB_H; i++) fb[i] = 0xFF000000;      // 帯は黒
    palette_init();
    sprite_init();
    short none[1];
    sound_fill(none, 0);                 // 音源を初めの状態に（OS では起動時に sound_init が最初に呼ぶ）
}
