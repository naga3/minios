// MML で書いた曲を、ファミコンの音源（apu.c）で演奏する。1/60 秒ごとに mml_frame が呼ばれて進む
// （音を作る処理の途中から呼ばれるので、リズムがぶれない）。
//
// 曲は 4 つの文字列（矩形波 1、矩形波 2、三角波、ノイズ）。命令は小文字（L だけ大文字）。空白は飛ばす。
//   c d e f g a b   音。後ろに + か # で半音上、- で半音下。数字で長さ（4 = 4 分音符）、. で 1.5 倍
//   r               休み（長さの書き方は音と同じ）
//   ^8              直前の音を伸ばす（タイ）
//   o4 < >          オクターブ（0〜7）、1 つ下げる・上げる
//   l8              長さを省いたときの長さ
//   t150            テンポ（4 分音符が 1 分に何回。全部のチャンネルに効く）
//   v12             音量（0〜15）
//   @2              音色（矩形波: 0 = 12.5%、1 = 25%、2 = 50%、3 = 75%。ノイズ: 0 = 長い周期、1 = 短い周期）
//   @v1             音量の動き（エンベロープ）の番号。書かなければ一定
//   q7              鳴らす長さ（音の長さの 8 分のいくつ）。残りは「離した後」
//   [ ... ]3        3 回繰り返す（入れ子は 4 段まで）
//   L               曲の終わりまで来たら、ここへ戻って繰り返す
// ノイズのチャンネルでは、音の高さがノイズの高さになる（1 オクターブと少しで 16 段。高い音ほど高いノイズ）。
//
// エンベロープは "15 13 11 9 | 8 / 6 4 2 1 0" のように書く（1/60 秒ごとの音量 0〜15）。
// | から / の前までを、押している間繰り返す。/ から後ろは離した後に進む（無ければ離すとすぐ 0）。
// 実際の音量 = v × エンベロープ ÷ 15。本物のファミコンと同じく 16 段だが、ゆっくり下げたり、余韻を付けたりできる
#include "sound.h"

enum { TRACKS = 4, ENVS = 8, ENV_LEN = 64, DEPTH = 4 };

typedef struct {
    signed char v[ENV_LEN];
    int len, loop, release;         // loop: | の位置、release: / の位置（無ければ -1）
} Envelope;

typedef struct {
    const char *p, *loop;           // 今読んでいる位置、L の位置
    int active;                     // 曲がまだ続いている
    int octave, length, volume, tone, env, quant;
    long wait;                      // 次の命令までの残り（1/256 フレーム）
    long gate;                      // 離すまでの残り（1/256 フレーム）
    int note;                       // 鳴らしている音（MIDI の番号。-1 は無し）
    int key;                        // 押している
    int env_pos;
    int last_hi;                    // 最後に書いた周期の上位（書き直すと波形が頭に戻ってプツッと鳴るので、変わったときだけ書く）
    struct { const char *p; int count; } stack[DEPTH];
    int depth;
} Track;

static struct {
    Track t[TRACKS];
    Envelope env[ENVS];
    int tempo;
    int playing;
} m;

static int number(const char **p, int def) {
    if (**p < '0' || **p > '9') return def;
    int n = 0;
    while (**p >= '0' && **p <= '9') n = n * 10 + *(*p)++ - '0';
    return n;
}

static void skip_spaces(const char **p) {
    while (**p == ' ' || **p == '\t' || **p == '\n') (*p)++;
}

// 長さ（1/256 フレーム）: 全音符 = 4 拍 = 4 × 3,600 ÷ テンポ フレーム
static long duration(Track *t) {
    long whole = 256L * 4 * 3600 / (m.tempo > 0 ? m.tempo : 120);
    int n = number(&t->p, t->length);
    long d = whole / (n > 0 ? n : 4), add = d;
    while (*t->p == '.') {
        t->p++;
        add /= 2;
        d += add;
    }
    return d;
}

// MIDI の番号の周波数（1/1000 Hz）: 4 オクターブ目の表から、オクターブの分だけずらす
static long note_mhz(int n) {
    static const long c4[12] = { 261626, 277183, 293665, 311127, 329628, 349228,
                                 369994, 391995, 415305, 440000, 466164, 493883 };
    int oct = n / 12 - 5;
    long f = c4[n % 12];
    return oct >= 0 ? f << oct : f >> -oct;
}

// 周期 = 1,789,773 ÷ (div × 周波数) - 1（矩形波は div = 16、三角波は 32）。11 ビットに収める
static int period(int n, int div) {
    long t = 1789773000LL / (div * note_mhz(n)) - 1;
    return t < 8 ? 8 : t > 0x7ff ? 0x7ff : (int)t;
}

static void note_on(Track *t, int ch, int n) {
    t->note = n;
    t->key = 1;
    t->env_pos = 0;
    if (ch < 2) {
        int per = period(n, 16), base = 0x4000 + ch * 4;
        apu_write(base + 2, per & 0xff);
        if ((per >> 8) != t->last_hi) {
            apu_write(base + 3, 1 << 3 | per >> 8);
            t->last_hi = per >> 8;
        }
    } else if (ch == 2) {
        int per = period(n, 32);
        apu_write(0x4008, 0xff);
        apu_write(0x400a, per & 0xff);
        apu_write(0x400b, 1 << 3 | per >> 8);
    } else {
        apu_write(0x400e, (t->tone & 1) << 7 | (15 - n % 16));
        apu_write(0x400f, 1 << 3);
    }
}

static void key_off(Track *t) {
    t->key = 0;
    Envelope *e = t->env >= 0 ? &m.env[t->env] : 0;
    if (e && e->release >= 0 && t->env_pos < e->release) t->env_pos = e->release;
}

// 次に時間を使う命令（音か休み）まで読む
static void read_track(Track *t, int ch) {
    static const int semitone[7] = { 9, 11, 0, 2, 4, 5, 7 };   // a b c d e f g
    while (t->active && t->wait <= 0) {
        char c = *t->p;
        if (!c) {
            if (t->loop) {
                t->p = t->loop;
                continue;
            }
            t->active = 0;
            key_off(t);
            break;
        }
        t->p++;
        switch (c) {
        case 'o': t->octave = number(&t->p, 4); break;
        case '<': t->octave--; break;
        case '>': t->octave++; break;
        case 'l': t->length = number(&t->p, 4); break;
        case 't': m.tempo = number(&t->p, 120); break;
        case 'v': t->volume = number(&t->p, 15) & 15; break;
        case 'q': {
            int q = number(&t->p, 8);
            t->quant = q < 1 ? 1 : q > 8 ? 8 : q;
            break;
        }
        case '@':
            if (*t->p == 'v') {
                t->p++;
                int e = number(&t->p, 0);
                t->env = e < ENVS && m.env[e].len ? e : -1;
            } else {
                t->tone = number(&t->p, 0) & 3;
            }
            break;
        case 'L': t->loop = t->p; break;
        case '[':
            if (t->depth < DEPTH) {
                t->stack[t->depth].p = t->p;
                t->stack[t->depth].count = -1;
                t->depth++;
            }
            break;
        case ']': {
            int n = number(&t->p, 2);
            if (t->depth == 0) break;
            int *count = &t->stack[t->depth - 1].count;
            if (*count < 0) *count = n;
            if (--*count > 0) t->p = t->stack[t->depth - 1].p;
            else t->depth--;
            break;
        }
        case 'r':
            key_off(t);
            t->wait += duration(t);
            break;
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f': case 'g': {
            int n = (t->octave + 1) * 12 + semitone[c - 'a'];
            while (*t->p == '+' || *t->p == '#' || *t->p == '-') n += *t->p++ == '-' ? -1 : 1;
            long d = duration(t);
            for (;;) {                                  // タイ: 続く ^ の長さを足す
                const char *q = t->p;
                skip_spaces(&q);
                if (*q != '^') break;
                t->p = q + 1;
                d += duration(t);
            }
            note_on(t, ch, n < 0 ? 0 : n);
            t->wait += d;
            t->gate = d * t->quant / 8;
            break;
        }
        default:                                        // 空白や知らない文字は飛ばす
            break;
        }
    }
}

// 今の音量（0〜15）。エンベロープを 1 つ進める
static int envelope_volume(Track *t) {
    if (t->env < 0) return t->key ? t->volume : 0;
    Envelope *e = &m.env[t->env];
    int v = e->v[t->env_pos < e->len ? t->env_pos : e->len - 1];
    if (!t->key && e->release < 0) v = 0;
    int end = e->release >= 0 ? e->release : e->len;    // 押している間に進める範囲
    if (t->key) {
        if (t->env_pos + 1 < end) t->env_pos++;
        else if (e->loop >= 0) t->env_pos = e->loop;
    } else if (t->env_pos + 1 < e->len) {
        t->env_pos++;
    }
    return (t->volume * v + 7) / 15;
}

void mml_frame(void) {
    if (!m.playing) return;
    int any = 0;
    for (int ch = 0; ch < TRACKS; ch++) {
        Track *t = &m.t[ch];
        read_track(t, ch);
        if (t->key && t->gate > 0 && (t->gate -= 256) <= 0) key_off(t);
        t->wait -= 256;
        int vol = t->note >= 0 ? envelope_volume(t) : 0;
        if (ch < 2) apu_write(0x4000 + ch * 4, t->tone << 6 | 0x30 | vol);     // 長さで止めない・音量は一定（ここで決める）
        else if (ch == 2) apu_write(0x4008, t->key ? 0xff : 0x80);            // 三角波は音量が無い。離したら止める
        else apu_write(0x400c, 0x30 | vol);
        any |= t->active || vol > 0;
    }
    m.playing = any;
}

static void parse_envelope(Envelope *e, const char *s) {
    *e = (Envelope){ .loop = -1, .release = -1 };
    if (!s) return;
    while (*s && e->len < ENV_LEN) {
        if (*s == '|') e->loop = e->len;
        else if (*s == '/') e->release = e->len;
        else if (*s >= '0' && *s <= '9') {
            int v = number(&s, 0);
            e->v[e->len++] = v > 15 ? 15 : v;
            continue;
        }
        s++;
    }
}

void mml_play(const Song *song) {
    unsigned long f = irq_off();
    m = (__typeof__(m)){0};
    m.tempo = 120;
    for (int i = 0; i < ENVS; i++) parse_envelope(&m.env[i], song->envelope[i]);
    for (int ch = 0; ch < TRACKS; ch++) {
        Track *t = &m.t[ch];
        t->p = song->track[ch] ? song->track[ch] : "";
        t->active = song->track[ch] != 0;
        t->octave = 4;
        t->length = 4;
        t->volume = 15;
        t->tone = ch < 2 ? 2 : 0;
        t->env = -1;
        t->quant = 8;
        t->note = -1;
        t->last_hi = -1;
    }
    apu_write(0x4015, 0x0f);
    apu_write(0x4001, 0x08);                // スイープは使わない（引き算にしておくと低い音も消えない）
    apu_write(0x4005, 0x08);
    apu_write(0x4017, 0x40);
    m.playing = 1;
    irq_restore(f);
}

void mml_stop(void) {
    unsigned long f = irq_off();
    m.playing = 0;
    apu_write(0x4000, 0x30);
    apu_write(0x4004, 0x30);
    apu_write(0x4008, 0x80);
    apu_write(0x400c, 0x30);
    irq_restore(f);
}

int mml_playing(void) { return m.playing; }
