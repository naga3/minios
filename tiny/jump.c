// ジャンプ: 横から見た 1 画面のステージを、ロボットが歩いて跳ぶ（jump.s を C に書き直したもの）
#include "tiny.h"

enum {
    TILE = 16, MAP_W = 20, MAP_H = 12,
    TOP = 8,                  // ステージを描き始める画面の y（1 行目は文字）
    FRAME = 2,                // 何回のタイマー（10ms）で 1 コマ進むか（50 コマ/秒）
    SKY = RGB6(2, 4, 5),
    // 速さは 1/16 点の単位、1 コマあたり
    ACCEL = 3, FRICTION = 4, MAX_RUN = 40, GRAVITY = 6, JUMP_V = 116, MAX_FALL = 112,
    HB_L = 3, HB_W = 10,      // 当たりの四角: 絵の左から HB_L 点目から幅 HB_W、高さ 16
};

// ステージ: . 空、G 草の地面、# 土、B れんが、? 箱（叩くと U）、C 雲（通れる）
static const char stage[MAP_H][MAP_W + 1] = {
    "....................",
    "..CC.........CC.....",
    "....................",
    "....................",
    "...........???......",
    "....................",
    "....................",
    "....B?B........BBB..",
    "....................",
    "........GG..........",
    "GGGGGGGG##GG..GGGGGG",
    "############..######",
};
static char level[MAP_H][MAP_W];

static struct {
    long x, y, vx, vy;        // 絵の左上（1/16 点）と速さ
    long anim;
    int facing, on_ground, jump_held;
} p;

extern const char pic_robot_stand[], pic_robot_walk[], pic_robot_jump[];
extern const char pic_grass[], pic_dirt[], pic_brick[], pic_question[], pic_used[], pic_cloud[];

// 点 (x, y) のマス。ステージの外なら 0
static char *tile_at(long x, long y) {
    long c = x >> 4, r = y >> 4;
    if (c < 0 || c >= MAP_W || r < 0 || r >= MAP_H) return 0;
    return &level[r][c];
}

// 通れないか。ステージの左右の外は壁、上下の外は空
static int solid(long x, long y) {
    char *t = tile_at(x, y);
    if (!t) return x < 0 || x >= MAP_W * TILE;
    return *t != '.' && *t != 'C';
}

// 当たりの左右の端のどちらかがマスか
static int solid2(long x, long y) { return solid(x, y) || solid(x + HB_W - 1, y); }

static void collide_x(void) {
    if (!p.vx) return;
    long x = (p.x >> 4) + HB_L + (p.vx > 0 ? HB_W - 1 : 0), y = p.y >> 4;
    if (!solid(x, y) && !solid(x, y + 7) && !solid(x, y + 15)) return;
    x &= -TILE;
    x = p.vx > 0 ? x - HB_L - HB_W : x + TILE - HB_L;
    p.x = x << 4;
    p.vx = 0;
}

static void collide_y(void) {
    long x = (p.x >> 4) + HB_L, y = p.y >> 4;
    if (p.vy > 0 && solid2(x, y + 15)) {            // 着地
        p.y = (((y + 15) & -TILE) - 16) << 4;
        p.vy = 0;
    } else if (p.vy < 0 && solid2(x, y)) {          // 頭を打つ。真ん中の上が ? なら使った箱に
        char *t = tile_at(x + HB_W / 2, y);
        if (t && *t == '?') *t = 'U';
        p.y = ((y | (TILE - 1)) + 1) << 4;
        p.vy = 0;
    }
    p.on_ground = solid2(x, (p.y >> 4) + 16);
}

// 1 コマ動かす。穴に落ちたら 1
static int update(void) {
    int left = keys[SC_LEFT] | keys[SC_A], right = keys[SC_RIGHT] | keys[SC_D];
    if (right && !left) {
        p.vx = p.vx + ACCEL > MAX_RUN ? MAX_RUN : p.vx + ACCEL;
        p.facing = 0;
    } else if (left && !right) {
        p.vx = p.vx - ACCEL < -MAX_RUN ? -MAX_RUN : p.vx - ACCEL;
        p.facing = 1;
    } else if (p.vx > 0) {
        p.vx = p.vx > FRICTION ? p.vx - FRICTION : 0;
    } else if (p.vx < 0) {
        p.vx = p.vx < -FRICTION ? p.vx + FRICTION : 0;
    }
    int jump = keys[SC_Z] | keys[SC_SPACE] | keys[SC_UP] | keys[SC_W];
    if (jump && !p.jump_held && p.on_ground) p.vy = -JUMP_V;   // 押した瞬間だけ
    p.jump_held = jump;
    p.vy += p.vy < 0 && !jump ? GRAVITY * 3 : GRAVITY;          // 早く離すと小さく跳ぶ
    if (p.vy > MAX_FALL) p.vy = MAX_FALL;
    p.x += p.vx;
    collide_x();
    p.y += p.vy;
    collide_y();
    p.anim++;
    return (p.y >> 4) >= MAP_H * TILE;
}

static const char *tile_picture(char c) {
    switch (c) {
    case 'G': return pic_grass;
    case '#': return pic_dirt;
    case 'B': return pic_brick;
    case '?': return pic_question;
    case 'U': return pic_used;
    case 'C': return pic_cloud;
    }
    return 0;
}

static void draw(void) {
    fill_rect(0, TOP, MAP_W * TILE, MAP_H * TILE, SKY);
    for (int r = 0; r < MAP_H; r++)
        for (int c = 0; c < MAP_W; c++) {
            const char *pic = tile_picture(level[r][c]);
            if (pic) draw_sprite(c * TILE, TOP + r * TILE, pic, 0);
        }
    const char *pic = !p.on_ground ? pic_robot_jump : p.vx && (p.anim & 4) ? pic_robot_walk : pic_robot_stand;
    draw_sprite(p.x >> 4, TOP + (p.y >> 4), pic, p.facing);
    fill_rect(0, 0, SCREEN_W, TOP, 0);              // 1 行目（跳ぶと上にはみ出すので、毎回描き直す）
    text_color(15, 0);
    text_goto(0, 0);
    text_write("JUMP  arrows  Z/space:jump  Esc:quit");
    wait_vsync();
    present();
}

static void setup(void) {
    for (int r = 0; r < MAP_H; r++)
        for (int c = 0; c < MAP_W; c++) level[r][c] = stage[r][c];
    p.x = (2 * TILE) << 4;
    p.y = (9 * TILE) << 4;
    p.vx = p.vy = p.anim = 0;
    p.facing = p.on_ground = 0;
    p.jump_held = 1;          // 押しっぱなしのキーで、いきなり跳ばないように
    draw();
}

// Esc が来ていれば 1（ほかの文字は捨てる。シェルに残らないように）
static int esc_pressed(void) {
    unsigned char c;
    while ((c = getc_nowait()))
        if (c == KEY_ESC) return 1;
    return 0;
}

void jump_main(void) {
    text_cursor(0);
    for (;;) {
        setup();
        unsigned long next = ticks;
        for (;;) {
            while (ticks < next) halt();
            next += FRAME;
            if (esc_pressed()) goto quit;
            if (update()) break;
            draw();
        }
        text_color(15, 4);                      // 穴に落ちた: 1 秒見せて、やり直す（その間も Esc は効く）
        text_goto(17, 12);
        text_puts(" MISS ");
        unsigned long until = ticks + 100;
        while (ticks < until) {
            if (esc_pressed()) goto quit;
            halt();
        }
    }
quit:
    esc_pressed();
    text_cursor(1);
    text_color(7, 1);                           // シェルの色に戻して消す
    con_clear();
}
