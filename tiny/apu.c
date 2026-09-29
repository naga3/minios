// ファミコンの音源（2A03 の APU）をソフトで作る: 矩形波 2 音、三角波 1 音、ノイズ（DPCM はまだ無い）。
// レジスタ（$4000〜$4017）の意味は本物と同じ。apu_write(0x4000, 値) のように書いて鳴らす。
//
// 本物の CPU のクロック（1,789,773 Hz）で数え、1 サンプル（22,050 Hz なので約 81 クロック）の間の値を平均して出す
// （高い音でもざらつかないように）。数える単位は 1/256 クロック
enum {
    CLOCK = 1789773,
#ifndef RATE
    RATE = 48000,
#endif
    SAMPLE = (int)((256LL * CLOCK) / RATE),     // 1 サンプルの長さ（1/256 クロック）
    QUARTER = 7457 * 256,                       // フレームカウンタの 1/4（240 Hz）
};

static const unsigned char length_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};
static const unsigned char duty_table[4][8] = {        // 12.5%、25%、50%、75%
    { 0, 1, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 1, 1, 0, 0, 0 },
    { 1, 0, 0, 1, 1, 1, 1, 1 },
};
static const unsigned char triangle_table[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};
static const unsigned short noise_table[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};

// 音量の自動変化（エンベロープ）: 一定か、15 から 0 へ下がっていく（繰り返しもできる）
typedef struct {
    int start, divider, decay;
    int constant, loop, volume;             // $4000 などの下位 6 ビット
} Envelope;

typedef struct {
    Envelope env;
    int enabled, length, halt;
    int timer;                              // 11 ビットの周期
    int counter;                            // 次に進むまで（1/256 クロック）
    int step;                               // 波形の何番目か
    int duty;
    int sweep_on, sweep_period, sweep_negate, sweep_shift, sweep_divider, sweep_reload;
    int ones_complement;                    // 矩形波 1 はスイープの引き算が 1 だけ多い
    int level;                              // 今の出力の高さ（重みをかけた後）
} Pulse;

static struct {
    Pulse pulse[2];
    struct {
        int enabled, length, control, reload_value, linear, reload;
        int timer, counter, step, level;
    } tri;
    struct {
        Envelope env;
        int enabled, length, halt;
        int period, counter, mode;
        unsigned shift;                     // 15 ビットの疑似乱数（LFSR）
        int level;
    } noise;
    int frame_counter, frame_step, five_step;
    long acc;                               // 段差を積み上げた今の値（× 32768）
    int dc;                                 // 直流分（1/256）
} apu;

static void envelope_clock(Envelope *e) {
    if (e->start) {
        e->start = 0;
        e->decay = 15;
        e->divider = e->volume;
    } else if (e->divider > 0) {
        e->divider--;
    } else {
        e->divider = e->volume;
        if (e->decay > 0) e->decay--;
        else if (e->loop) e->decay = 15;
    }
}

static int envelope_out(const Envelope *e) { return e->constant ? e->volume : e->decay; }

static int sweep_target(const Pulse *p) {
    int change = p->timer >> p->sweep_shift;
    if (p->sweep_negate) return p->timer - change - p->ones_complement;
    return p->timer + change;
}

static int pulse_muted(const Pulse *p) { return p->timer < 8 || sweep_target(p) > 0x7ff; }

// 1/4 フレーム: エンベロープと三角波のリニアカウンタ
static void quarter_frame(void) {
    envelope_clock(&apu.pulse[0].env);
    envelope_clock(&apu.pulse[1].env);
    envelope_clock(&apu.noise.env);
    if (apu.tri.reload) apu.tri.linear = apu.tri.reload_value;
    else if (apu.tri.linear > 0) apu.tri.linear--;
    if (!apu.tri.control) apu.tri.reload = 0;
}

// 1/2 フレーム: 長さのカウンタとスイープ
static void half_frame(void) {
    for (int i = 0; i < 2; i++) {
        Pulse *p = &apu.pulse[i];
        if (p->length > 0 && !p->halt) p->length--;
        if (p->sweep_divider == 0 && p->sweep_on && p->sweep_shift > 0 && !pulse_muted(p)) p->timer = sweep_target(p);
        if (p->sweep_divider == 0 || p->sweep_reload) {
            p->sweep_divider = p->sweep_period;
            p->sweep_reload = 0;
        } else {
            p->sweep_divider--;
        }
    }
    if (apu.tri.length > 0 && !apu.tri.control) apu.tri.length--;
    if (apu.noise.length > 0 && !apu.noise.halt) apu.noise.length--;
}

static void frame_step(void) {
    // 4 ステップ: 1/4 を毎回、1/2 を 2 回目と 4 回目。5 ステップ: 5 回目は何もしない
    int s = apu.frame_step;
    apu.frame_step = (s + 1) % (apu.five_step ? 5 : 4);
    if (apu.five_step && s == 3) return;
    quarter_frame();
    if (s == 1 || s == (apu.five_step ? 4 : 3)) half_frame();
}

// 割り込みを止める・元に戻す（止める前の状態に戻すので、割り込みの中から呼んでもよい）
#ifdef HOST                                     // PC の上で試すとき（tools/apurender.c）は割り込みが無い
static unsigned long irq_off(void) { return 0; }
static void irq_restore(unsigned long f) { (void)f; }
#else
static unsigned long irq_off(void) {
    unsigned long f;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(f) :: "memory");
    return f;
}
static void irq_restore(unsigned long f) { __asm__ volatile("push %0; popfq" :: "r"(f) : "memory", "cc"); }
#endif

// レジスタに書く（割り込みの中で音を作っている途中に書き換えないように、割り込みを止めて書く）
void apu_write(int addr, int v) {
    unsigned long f = irq_off();
    if (addr >= 0x4000 && addr <= 0x4007) {
        Pulse *p = &apu.pulse[(addr - 0x4000) >> 2];
        switch (addr & 3) {
        case 0:                                 // DDLC VVVV: 音色、長さ止め・繰り返し、一定の音量、音量
            p->duty = v >> 6;
            p->halt = p->env.loop = (v >> 5) & 1;
            p->env.constant = (v >> 4) & 1;
            p->env.volume = v & 15;
            break;
        case 1:                                 // EPPP NSSS: スイープ
            p->sweep_on = v >> 7;
            p->sweep_period = (v >> 4) & 7;
            p->sweep_negate = (v >> 3) & 1;
            p->sweep_shift = v & 7;
            p->sweep_reload = 1;
            break;
        case 2:
            p->timer = (p->timer & 0x700) | v;
            break;
        case 3:                                 // LLLL Lttt: 長さ、周期の上位。鳴らし始める
            p->timer = (p->timer & 0xff) | (v & 7) << 8;
            if (p->enabled) p->length = length_table[v >> 3];
            p->step = 0;
            p->env.start = 1;
            break;
        }
    } else switch (addr) {
    case 0x4008:                                // CRRR RRRR: 止め、リニアカウンタ
        apu.tri.control = v >> 7;
        apu.tri.reload_value = v & 0x7f;
        break;
    case 0x400a:
        apu.tri.timer = (apu.tri.timer & 0x700) | v;
        break;
    case 0x400b:
        apu.tri.timer = (apu.tri.timer & 0xff) | (v & 7) << 8;
        if (apu.tri.enabled) apu.tri.length = length_table[v >> 3];
        apu.tri.reload = 1;
        break;
    case 0x400c:                                // --LC VVVV
        apu.noise.halt = apu.noise.env.loop = (v >> 5) & 1;
        apu.noise.env.constant = (v >> 4) & 1;
        apu.noise.env.volume = v & 15;
        break;
    case 0x400e:                                // M--- PPPP: 短い周期、周期
        apu.noise.mode = v >> 7;
        apu.noise.period = noise_table[v & 15];
        break;
    case 0x400f:
        if (apu.noise.enabled) apu.noise.length = length_table[v >> 3];
        apu.noise.env.start = 1;
        break;
    case 0x4015:                                // ---D NT21: 鳴らす音（切ると長さが 0 になる）
        for (int i = 0; i < 2; i++) {
            apu.pulse[i].enabled = (v >> i) & 1;
            if (!apu.pulse[i].enabled) apu.pulse[i].length = 0;
        }
        apu.tri.enabled = (v >> 2) & 1;
        if (!apu.tri.enabled) apu.tri.length = 0;
        apu.noise.enabled = (v >> 3) & 1;
        if (!apu.noise.enabled) apu.noise.length = 0;
        break;
    case 0x4017:                                // MI-- ----: 5 ステップ。書くとフレームカウンタが初めから
        apu.five_step = v >> 7;
        apu.frame_step = 0;
        apu.frame_counter = 0;
        if (apu.five_step) {
            quarter_frame();
            half_frame();
        }
        break;
    }
    irq_restore(f);
}

static void clear_steps(void);

void apu_reset(void) {
    unsigned long f = irq_off();
    apu = (__typeof__(apu)){0};
    apu.pulse[0].ones_complement = 1;
    apu.noise.shift = 1;
    apu.noise.period = noise_table[0];
    clear_steps();
    irq_restore(f);
    apu_write(0x4015, 0);
    apu_write(0x4017, 0);
}

// ---- 波形を作る ----
//
// 矩形波は角が急なので、サンプルの間隔より細かい高い音（倍音）をたくさん含む。そのまま標本化すると、
// 出せない高さの倍音が低い高さに折り返して、ザラザラした雑音になる（エイリアシング）。
// そこで、出力が変わった瞬間ごとに「出せない高さを含まない、なめらかな段差」（BLEP）を、ちょうどの時刻に置く。
// 段差の形（16 サンプル × 1/32 ずつずらした 32 通り）は PC の上で作った表（窓をかけた sinc、ナイキストの 0.9 倍まで）。
// 段差を差分として置いていき、最後に積み上げると波形になる。出力は 8 サンプル遅れる

static const short blep[32][16] = {
    { 18, -110, 359, -843, 1561, -2371, 3025, 29490, 3025, -2371, 1561, -843, 359, -110, 18, 0 },
    { 17, -108, 347, -795, 1421, -2025, 2117, 29452, 3974, -2714, 1693, -887, 369, -111, 18, 0 },
    { 17, -105, 332, -742, 1276, -1679, 1252, 29332, 4960, -3051, 1818, -925, 376, -110, 17, 0 },
    { 16, -102, 315, -686, 1128, -1335, 434, 29131, 5981, -3378, 1932, -956, 380, -109, 17, 0 },
    { 16, -98, 297, -627, 977, -997, -336, 28853, 7031, -3693, 2036, -982, 381, -106, 16, 0 },
    { 15, -93, 277, -566, 824, -665, -1055, 28499, 8106, -3992, 2127, -999, 378, -103, 15, 0 },
    { 14, -87, 256, -503, 672, -343, -1721, 28067, 9203, -4273, 2204, -1009, 372, -97, 13, 0 },
    { 13, -82, 234, -439, 522, -34, -2334, 27565, 10317, -4531, 2266, -1011, 362, -91, 11, 0 },
    { 12, -76, 211, -375, 374, 262, -2891, 26992, 11444, -4765, 2311, -1004, 348, -83, 8, 0 },
    { 10, -69, 188, -311, 229, 543, -3394, 26350, 12577, -4970, 2339, -987, 330, -73, 6, 0 },
    { 9, -63, 165, -248, 90, 807, -3840, 25646, 13712, -5144, 2348, -962, 308, -62, 2, 0 },
    { 8, -56, 142, -186, -44, 1052, -4231, 24877, 14845, -5283, 2338, -926, 282, -50, -1, 1 },
    { 7, -50, 119, -126, -171, 1277, -4566, 24057, 15970, -5386, 2307, -881, 251, -36, -5, 1 },
    { 6, -44, 96, -68, -291, 1482, -4846, 23182, 17081, -5448, 2255, -825, 217, -21, -10, 2 },
    { 5, -37, 74, -12, -403, 1666, -5072, 22257, 18174, -5467, 2182, -760, 178, -4, -15, 2 },
    { 4, -31, 53, 41, -506, 1828, -5246, 21289, 19243, -5441, 2086, -685, 136, 14, -20, 3 },
    { 3, -25, 33, 90, -600, 1968, -5368, 20283, 20283, -5368, 1968, -600, 90, 33, -25, 3 },
    { 3, -20, 14, 136, -685, 2086, -5441, 19243, 21289, -5246, 1828, -506, 41, 53, -31, 4 },
    { 2, -15, -4, 178, -760, 2182, -5467, 18175, 22256, -5072, 1666, -403, -12, 74, -37, 5 },
    { 2, -10, -21, 217, -825, 2255, -5448, 17083, 23180, -4846, 1482, -291, -68, 96, -44, 6 },
    { 1, -5, -36, 251, -881, 2307, -5386, 15972, 24055, -4566, 1277, -171, -126, 119, -50, 7 },
    { 1, -1, -50, 282, -926, 2338, -5283, 14844, 24878, -4231, 1052, -44, -186, 142, -56, 8 },
    { 0, 2, -62, 308, -962, 2348, -5144, 13714, 25644, -3840, 807, 90, -248, 165, -63, 9 },
    { 0, 6, -73, 330, -987, 2339, -4970, 12577, 26350, -3394, 543, 229, -311, 188, -69, 10 },
    { 0, 8, -83, 348, -1004, 2311, -4765, 11445, 26991, -2891, 262, 374, -375, 211, -76, 12 },
    { 0, 11, -91, 362, -1011, 2266, -4531, 10317, 27565, -2334, -34, 522, -439, 234, -82, 13 },
    { 0, 13, -97, 372, -1009, 2204, -4273, 9202, 28068, -1721, -343, 672, -503, 256, -87, 14 },
    { 0, 15, -103, 378, -999, 2127, -3992, 8106, 28499, -1055, -665, 824, -566, 277, -93, 15 },
    { 0, 16, -106, 381, -982, 2036, -3693, 7030, 28854, -336, -997, 977, -627, 297, -98, 16 },
    { 0, 17, -109, 380, -956, 1932, -3378, 5980, 29132, 434, -1335, 1128, -686, 315, -102, 16 },
    { 0, 17, -110, 376, -925, 1818, -3051, 4960, 29332, 1252, -1679, 1276, -742, 332, -105, 17 },
    { 0, 18, -111, 369, -887, 1693, -2714, 3974, 29452, 2117, -2025, 1421, -795, 347, -108, 17 },
};

enum { TAPS = 16, PHASES = 32, BLOCK = 1024 };
enum { W_PULSE = 301, W_TRI = 340, W_NOISE = 198 };  // 混ぜる重み（本物の出力の比 0.00752 : 0.00851 : 0.00494）
static long delta[BLOCK + TAPS];                    // 段差の差分（× 32768）

// ブロックの初めから t（1/256 クロック）の時刻に、出力が d だけ変わった
static void add_step(long t, int d) {
    long pos = t * PHASES / SAMPLE;
    const short *k = blep[pos % PHASES];
    long *out = &delta[pos / PHASES];
    for (int j = 0; j < TAPS; j++) out[j] += (long)d * k[j];
}

static void clear_steps(void) {
    for (int j = 0; j < BLOCK + TAPS; j++) delta[j] = 0;
}

static void set_level(int *level, int v, long t) {
    if (v != *level) {
        add_step(t, v - *level);
        *level = v;
    }
}

static int pulse_level(const Pulse *p) {
    if (p->length == 0 || pulse_muted(p) || !duty_table[p->duty][p->step]) return 0;
    return envelope_out(&p->env) * W_PULSE;
}

static int triangle_running(void) {         // 周期 2 未満は聞こえない高さなので止める
    return apu.tri.length > 0 && apu.tri.linear > 0 && apu.tri.timer >= 2;
}

static int noise_level(void) {
    if (apu.noise.length == 0 || (apu.noise.shift & 1)) return 0;
    return envelope_out(&apu.noise.env) * W_NOISE;
}

// 音量の変化やレジスタの書き換えを、時刻 t の段差にする
static void update_levels(long t) {
    set_level(&apu.pulse[0].level, pulse_level(&apu.pulse[0]), t);
    set_level(&apu.pulse[1].level, pulse_level(&apu.pulse[1]), t);
    set_level(&apu.tri.level, triangle_table[apu.tri.step] * W_TRI, t);
    set_level(&apu.noise.level, noise_level(), t);
}

// [t0, t1) の間、周期ごとに波形を進め、変わった時刻に段差を置く。counter は次に進むまでの時間
static void pulse_run(Pulse *p, long t0, long t1) {
    long period = (p->timer + 1) * 2 * 256, t = t0;
    while (p->counter <= t1 - t) {
        t += p->counter;
        p->counter = period;
        p->step = (p->step + 1) & 7;
        set_level(&p->level, pulse_level(p), t);
    }
    p->counter -= t1 - t;
}

static void triangle_run(long t0, long t1) {
    if (!triangle_running()) return;        // 止まると今の高さのまま
    long period = (apu.tri.timer + 1) * 256, t = t0;
    while (apu.tri.counter <= t1 - t) {
        t += apu.tri.counter;
        apu.tri.counter = period;
        apu.tri.step = (apu.tri.step + 1) & 31;
        set_level(&apu.tri.level, triangle_table[apu.tri.step] * W_TRI, t);
    }
    apu.tri.counter -= t1 - t;
}

static void noise_run(long t0, long t1) {
    long period = apu.noise.period * 256, t = t0;
    while (apu.noise.counter <= t1 - t) {
        t += apu.noise.counter;
        apu.noise.counter = period;
        unsigned s = apu.noise.shift;
        unsigned fb = (s ^ (s >> (apu.noise.mode ? 6 : 1))) & 1;
        apu.noise.shift = (s >> 1) | fb << 14;
        set_level(&apu.noise.level, noise_level(), t);
    }
    apu.noise.counter -= t1 - t;
}

static void render_block(short *buf, int n) {
    long end = (long)n * SAMPLE, t = 0;
    update_levels(0);                       // 前のブロックの後に書かれたレジスタ
    while (t < end) {                       // フレームカウンタの区切りごとに進める
        long seg = QUARTER - apu.frame_counter;
        long t1 = t + seg < end ? t + seg : end;
        pulse_run(&apu.pulse[0], t, t1);
        pulse_run(&apu.pulse[1], t, t1);
        triangle_run(t, t1);
        noise_run(t, t1);
        apu.frame_counter += t1 - t;
        t = t1;
        if (apu.frame_counter >= QUARTER) {
            apu.frame_counter -= QUARTER;
            frame_step();
            update_levels(t);
        }
    }
    for (int i = 0; i < n; i++) {           // 段差を積み上げて波形に。出口で直流分を取り除く
        apu.acc += delta[i];
        int x = (int)(apu.acc >> 15);
        apu.dc += x - (apu.dc >> 8);
        int y = x - (apu.dc >> 8) + buf[i];
        buf[i] = y > 32767 ? 32767 : y < -32768 ? -32768 : y;
    }
    for (int j = 0; j < TAPS; j++) delta[j] = delta[n + j];    // はみ出した分は次のブロックへ
    for (int j = TAPS; j < n + TAPS; j++) delta[j] = 0;
}

// n サンプルを作って buf に足す（sound.c が割り込みの中から呼ぶ）
void apu_render(short *buf, int n) {
    while (n > 0) {
        int k = n < BLOCK ? n : BLOCK;
        render_block(buf, k);
        buf += k;
        n -= k;
    }
}
