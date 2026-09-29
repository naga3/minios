// ファミコンの音源（2A03 の APU）をソフトで作る: 矩形波 2 音、三角波 1 音、ノイズ（DPCM はまだ無い）。
// レジスタ（$4000〜$4017）の意味は本物と同じ。apu_write(0x4000, 値) のように書いて鳴らす。
//
// 本物の CPU のクロック（1,789,773 Hz）で数え、1 サンプル（22,050 Hz なので約 81 クロック）の間の値を平均して出す
// （高い音でもざらつかないように）。数える単位は 1/256 クロック
enum {
    CLOCK = 1789773, RATE = 22050,
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
} Pulse;

static struct {
    Pulse pulse[2];
    struct {
        int enabled, length, control, reload_value, linear, reload;
        int timer, counter, step;
    } tri;
    struct {
        Envelope env;
        int enabled, length, halt;
        int period, counter, mode;
        unsigned shift;                     // 15 ビットの疑似乱数（LFSR）
    } noise;
    int frame_counter, frame_step, five_step;
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

void apu_reset(void) {
    unsigned long f = irq_off();
    apu = (__typeof__(apu)){0};
    apu.pulse[0].ones_complement = 1;
    apu.noise.shift = 1;
    apu.noise.period = noise_table[0];
    irq_restore(f);
    apu_write(0x4015, 0);
    apu_write(0x4017, 0);
}

// 1 サンプル分の値の合計（値 × 1/256 クロック）。周期ごとに波形を進めながら数える
static int pulse_sample(Pulse *p) {
    int active = p->length > 0 && !pulse_muted(p);
    int vol = active ? envelope_out(&p->env) : 0, period = (p->timer + 1) * 2 * 256;
    int sum = 0, left = SAMPLE;
    while (left > 0) {
        if (p->counter <= 0) {
            p->counter += period;
            p->step = (p->step + 1) & 7;
        }
        int d = p->counter < left ? p->counter : left;
        if (duty_table[p->duty][p->step]) sum += vol * d;
        p->counter -= d;
        left -= d;
    }
    return sum;
}

static int triangle_sample(void) {
    int run = apu.tri.length > 0 && apu.tri.linear > 0 && apu.tri.timer >= 2;   // 周期 2 未満は聞こえない高さなので止める
    int period = (apu.tri.timer + 1) * 256, sum = 0, left = SAMPLE;
    if (!run) return triangle_table[apu.tri.step] * SAMPLE;     // 止まると今の高さのまま
    while (left > 0) {
        if (apu.tri.counter <= 0) {
            apu.tri.counter += period;
            apu.tri.step = (apu.tri.step + 1) & 31;
        }
        int d = apu.tri.counter < left ? apu.tri.counter : left;
        sum += triangle_table[apu.tri.step] * d;
        apu.tri.counter -= d;
        left -= d;
    }
    return sum;
}

static int noise_sample(void) {
    int vol = apu.noise.length > 0 ? envelope_out(&apu.noise.env) : 0;
    int period = apu.noise.period * 256, sum = 0, left = SAMPLE;
    while (left > 0) {
        if (apu.noise.counter <= 0) {
            apu.noise.counter += period;
            unsigned s = apu.noise.shift;
            unsigned fb = (s ^ (s >> (apu.noise.mode ? 6 : 1))) & 1;
            apu.noise.shift = (s >> 1) | fb << 14;
        }
        int d = apu.noise.counter < left ? apu.noise.counter : left;
        if (!(apu.noise.shift & 1)) sum += vol * d;
        apu.noise.counter -= d;
        left -= d;
    }
    return sum;
}

// n サンプルを作って buf に足す（sound.c が割り込みの中から呼ぶ）
void apu_render(short *buf, int n) {
    for (int i = 0; i < n; i++) {
        apu.frame_counter += SAMPLE;
        while (apu.frame_counter >= QUARTER) {
            apu.frame_counter -= QUARTER;
            frame_step();
        }
        // 平均した値（0〜15）× 256。混ぜる重みは本物の出力に近い比（矩形波 0.00752、三角波 0.00851、ノイズ 0.00494）
        long p = (long)(pulse_sample(&apu.pulse[0]) + pulse_sample(&apu.pulse[1])) * 256 / SAMPLE;
        long t = (long)triangle_sample() * 256 / SAMPLE, ns = (long)noise_sample() * 256 / SAMPLE;
        int x = (int)((p * 752 + t * 851 + ns * 494) * 2 / 5 / 256);   // 全部最大で 17,000 ほど（直流を取った後に振れても割れない）
        apu.dc += x - (apu.dc >> 8);                                    // 直流分を取り除く（本物も出口で取り除かれる）
        int y = x - (apu.dc >> 8) + buf[i];
        buf[i] = y > 32767 ? 32767 : y < -32768 ? -32768 : y;
    }
}
