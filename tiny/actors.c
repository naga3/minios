// 動くもの: ロボット（プレイヤー）と、敵・アイテム・弾など。マスとの当たり、踏む・叩く・拾う
#include "robo.h"

Player pl;
Thing th[MAX_THINGS];
static Thing dummy;                 // いっぱいのときに new_thing が返す（書いても何も起きない）
static int next_spawn;              // 次に出す spawns の番号
static int multi_c = -1, multi_r, multi_left;   // コインが何枚も出るれんが

enum {
    GRAV = 6, GRAV_FAST = 18, MAX_FALL = 112,
    WALK_MAX = 36, RUN_MAX = 60, ACCEL = 3, AIR_ACCEL = 2, SKID = 6, FRICTION = 3,
    JUMP_V = 114, STOMP_V = 80, STOMP_V_HELD = 116,
};

static int iabs(int v) { return v < 0 ? -v : v; }

// ---- マスとの当たり ----

// 点 (x, y) が通れないか。ステージの左右の外は壁、上下の外は空。up なら隠れた箱（下から叩いたときだけ当たる）も
static int solid_px(int x, int y, int up) {
    if (x < 0 || x >= st.width * TILE) return 1;
    if (y < 0 || y >= ROWS * TILE) return 0;
    char c = map[y >> 4][x >> 4];
    return tile_solid(c) || (up && c == 'l');
}

// 縦の線 (x, y〜y+h-1) か 横の線 (x〜x+w-1, y) のどこかが通れないか（8 点おきと端を見る）
static int column_hits(int x, int y, int h) {
    for (int i = 0;; i += 8) {
        if (i > h - 1) i = h - 1;
        if (solid_px(x, y + i, 0)) return 1;
        if (i == h - 1) return 0;
    }
}

static int row_hits(int x, int w, int y, int up) {
    for (int i = 0;; i += 8) {
        if (i > w - 1) i = w - 1;
        if (solid_px(x + i, y, up)) return 1;
        if (i == w - 1) return 0;
    }
}

static int bump_c, bump_r;          // 最後に頭で叩いたマス

// 横に動かして押し戻し、次に縦。ぶつかった向きと、立っているかを返す
int body_move(Body *b, int is_player) {
    int res = 0;
    b->x += b->vx;
    if (b->vx) {
        int x = b->x >> 4, y = b->y >> 4;
        int ex = b->vx > 0 ? x + b->w - 1 : x;
        if (column_hits(ex, y, b->h)) {
            if (b->vx > 0) {
                x = (ex & -TILE) - b->w;
                res |= HIT_R;
            } else {
                x = (ex | (TILE - 1)) + 1;
                res |= HIT_L;
            }
            b->x = x << 4;
            b->vx = 0;
        }
    }
    b->y += b->vy;
    int x = b->x >> 4, y = b->y >> 4;
    if (b->vy > 0) {
        int ey = y + b->h - 1;
        if (row_hits(x, b->w, ey, 0)) {
            b->y = ((ey & -TILE) - b->h) << 4;
            b->vy = 0;
            res |= ON_GROUND;
        }
    } else if (b->vy < 0 && row_hits(x, b->w, y, is_player)) {
        int cx = x + b->w / 2;          // 叩いたマス: 頭の真ん中。そこが空なら当たった端
        if (!solid_px(cx, y, is_player)) cx = solid_px(x, y, is_player) ? x : x + b->w - 1;
        bump_c = cx >> 4;
        bump_r = y >> 4;
        b->y = ((y | (TILE - 1)) + 1) << 4;
        b->vy = 0;
        res |= HIT_U;
    }
    if (!(res & ON_GROUND) && b->vy >= 0 && row_hits(b->x >> 4, b->w, (b->y >> 4) + b->h, 0)) res |= ON_GROUND;
    return res;
}

static int overlap(const Body *a, const Body *b) {
    int ax = a->x >> 4, ay = a->y >> 4, bx = b->x >> 4, by = b->y >> 4;
    return ax < bx + b->w && bx < ax + a->w && ay < by + b->h && by < ay + a->h;
}

// ---- 数える ----

void add_score(int n) { G.score += n; }

void add_coin(void) {
    add_score(200);
    if (++G.coins >= 100) {
        G.coins = 0;
        G.lives++;
    }
}

// ---- もの ----

// 当たりの四角の大きさと、絵からのずれ
static const struct { signed char w, h, dx, dy; } shape[] = {
    [T_WALKER] = {12, 14, 2, 2}, [T_BEETLE] = {12, 14, 2, 2}, [T_SHELL] = {12, 14, 2, 2}, [T_FLYER] = {12, 14, 2, 2},
    [T_PLANT] = {10, 24, 3, 0}, [T_SPIKY] = {12, 14, 2, 2}, [T_CANNON] = {16, 16, 0, 0}, [T_BULLET] = {14, 10, 1, 3},
    [T_THROWER] = {12, 14, 2, 2}, [T_WRENCH] = {10, 10, 3, 3}, [T_FIREBAR] = {16, 16, 0, 0}, [T_BUBBLE] = {12, 12, 2, 2},
    [T_BOSS] = {28, 30, 2, 2}, [T_BOSSFIRE] = {16, 6, 0, 5}, [T_LIFT] = {48, 8, 0, 0}, [T_LIFT_V] = {48, 8, 0, 0},
    [T_BATTERY] = {14, 14, 1, 2}, [T_BLASTER] = {14, 14, 1, 2}, [T_STAR] = {14, 14, 1, 2}, [T_ONEUP] = {14, 14, 1, 2},
    [T_COINPOP] = {16, 16, 0, 0}, [T_SHOT] = {8, 8, 4, 4}, [T_DEBRIS] = {8, 8, 4, 4}, [T_FLAG] = {16, 16, 0, 0},
    [T_DEAD] = {16, 16, 0, 0},
};

// 絵の左上 (x, y)（点）に出す
Thing *new_thing(int type, int x, int y) {
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing *t = &th[i];
        if (t->type != T_NONE) continue;
        *t = (Thing){0};
        t->type = type;
        t->b.w = shape[type].w;
        t->b.h = shape[type].h;
        t->dx = shape[type].dx;
        t->dy = shape[type].dy;
        t->b.x = (x + t->dx) << 4;
        t->b.y = (y + t->dy) << 4;
        t->dir = -1;
        t->x0 = x;
        t->y0 = y;
        return t;
    }
    dummy = (Thing){0};
    return &dummy;
}

void actors_reset(void) {
    for (int i = 0; i < MAX_THINGS; i++) th[i].type = T_NONE;
    next_spawn = 0;
    multi_c = -1;
    G.bounce_t = 0;
}

// ロボットを col 列の地面の上に置く（下から見て、最初に上が空いているマス）
void player_place(int col) {
    int power = pl.power, r = 9;
    for (int rr = ROWS - 1; rr >= 1; rr--)
        if (tile_solid(map[rr][col]) && !tile_solid(map[rr - 1][col])) {
            r = rr - 1;
            break;
        }
    pl = (Player){0};
    pl.power = power;
    pl.b.w = 10;
    pl.b.h = power ? 32 : 16;
    pl.b.x = (col * TILE + 3) << 4;
    pl.b.y = ((r + 1) * TILE - pl.b.h) << 4;
    pl.jump_held = pl.fire_held = 1;
}

// 大きさを power に合わせる（足の位置はそのまま）
static void player_size(void) {
    int h = pl.power ? 32 : 16;
    if (pl.b.h != h) {
        pl.b.y += (pl.b.h - h) << 4;
        pl.b.h = h;
        pl.grow = 30;
    }
}

void kill_player(void) {
    if (G.god || G.mode != M_PLAY) return;
    G.mode = M_DYING;
    pl.power = 0;
    pl.b.vx = pl.b.vy = 0;
}

void hurt_player(void) {
    if (pl.hurt || pl.star || G.god) return;
    if (pl.power) {
        pl.power = 0;
        player_size();
        pl.hurt = 100;
    } else {
        kill_player();
    }
}

// ---- やられる ----

static const char *thing_pic(Thing *t, int *flip);

// 叩かれた・撃たれた: 逆さまになって落ちていく
static void knock_out(Thing *t, int score) {
    int flip;
    const char *p = thing_pic(t, &flip);
    t->pic = p;
    t->flip = flip | 2;
    t->type = T_DEAD;
    t->behind = 0;
    t->b.vy = -64;
    t->b.vx = (t->b.x > pl.b.x ? 1 : -1) * 12;
    add_score(score);
}

static int is_foe(int type) {       // 撃つ・甲羅・叩くで倒せる敵
    switch (type) {
    case T_WALKER: case T_BEETLE: case T_SHELL: case T_FLYER: case T_PLANT: case T_SPIKY: case T_THROWER:
        return 1;
    }
    return 0;
}

// ---- マスを叩く ----

static void coin_pop(int c, int r) {
    add_coin();
    Thing *t = new_thing(T_COINPOP, c * TILE, (r - 1) * TILE);
    t->b.vy = -96;
    t->timer = 24;
}

static void item_from_block(int type, int c, int r) {
    Thing *t = new_thing(type, c * TILE, r * TILE);
    t->behind = 1;                  // マスの後ろから出てくる
    t->timer = 16;
    t->dir = 1;
}

static void break_brick(int c, int r) {
    map[r][c] = '.';
    add_score(50);
    for (int i = 0; i < 4; i++) {
        Thing *t = new_thing(T_DEBRIS, c * TILE + (i & 1) * 8 - 4, r * TILE + (i >> 1) * 8 - 4);
        t->b.vx = i & 1 ? 20 : -20;
        t->b.vy = i >> 1 ? -64 : -100;
    }
}

static void bump_tile(int c, int r) {
    if (c < 0 || c >= st.width || r < 0 || r >= ROWS) return;
    char *m = &map[r][c];
    switch (*m) {
    case '?': coin_pop(c, r); *m = 'U'; break;
    case 'M': item_from_block(pl.power ? T_BLASTER : T_BATTERY, c, r); *m = 'U'; break;
    case 'S': item_from_block(T_STAR, c, r); *m = 'U'; break;
    case 'l': item_from_block(T_ONEUP, c, r); *m = 'U'; break;
    case '$':
        if (multi_c != c || multi_r != r) {
            multi_c = c;
            multi_r = r;
            multi_left = 8;
        }
        coin_pop(c, r);
        if (--multi_left <= 0) *m = 'U';
        break;
    case 'B':
        if (pl.power) break_brick(c, r);
        break;
    default:
        return;                     // 叩いても何も起きないマス
    }
    if (*m != '.') {
        G.bounce_c = c;
        G.bounce_r = r;
        G.bounce_t = 8;
    }
    // 上に乗っていた敵は跳ね飛ばし、アイテムは跳ねる
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing *t = &th[i];
        int x = t->b.x >> 4, bottom = (t->b.y >> 4) + t->b.h;
        if (!t->type || bottom < r * TILE - 4 || bottom > r * TILE + 2 || x + t->b.w <= c * TILE || x >= c * TILE + TILE)
            continue;
        if (is_foe(t->type) && t->type != T_PLANT) {
            knock_out(t, 100);
        } else if ((t->type == T_BATTERY || t->type == T_ONEUP || t->type == T_STAR) && t->state) {
            t->b.vy = -80;
            t->dir = x + t->b.w / 2 < c * TILE + 8 ? -1 : 1;
        }
    }
    if (r > 0 && map[r - 1][c] == 'o') {
        map[r - 1][c] = '.';
        coin_pop(c, r - 1);
    }
}

// ---- ロボット ----

static void shoot(void) {
    int n = 0;
    for (int i = 0; i < MAX_THINGS; i++) n += th[i].type == T_SHOT;
    if (n >= 2) return;
    int x = (pl.b.x >> 4) + (pl.facing ? -8 : pl.b.w), y = (pl.b.y >> 4) + 4;
    Thing *t = new_thing(T_SHOT, x - 4, y - 4);
    t->dir = pl.facing ? -1 : 1;
    t->b.vy = 32;
}

// 重なったマス: コインを拾う、溶岩でミス、斧で橋を落とす
static void touch_tiles(void) {
    int x = pl.b.x >> 4, y = pl.b.y >> 4;
    for (int r = y >> 4; r <= (y + pl.b.h - 1) >> 4; r++)
        for (int c = x >> 4; c <= (x + pl.b.w - 1) >> 4; c++) {
            if (r < 0 || r >= ROWS || c < 0 || c >= st.width) continue;
            char *m = &map[r][c];
            if (*m == 'o') {
                *m = '.';
                add_coin();
            } else if (*m == '~') {
                kill_player();
            } else if (*m == 'A' && G.mode == M_PLAY) {
                G.mode = M_AXE;
            }
        }
}

void player_update(void) {
    int left = keys[SC_LEFT] | keys[SC_A], right = keys[SC_RIGHT] | keys[SC_D];
    int jump = keys[SC_Z] | keys[SC_SPACE] | keys[SC_UP] | keys[SC_W];
    int fire = keys[SC_X] | keys[SC_LSHIFT] | keys[SC_RSHIFT];
    int max = fire ? RUN_MAX : WALK_MAX, acc = pl.on_ground ? ACCEL : AIR_ACCEL;
    int *vx = &pl.b.vx;
    if (right && !left) {
        if (*vx < 0 && pl.on_ground) *vx += SKID;
        else if (*vx < max) *vx += acc;
        pl.facing = 0;
    } else if (left && !right) {
        if (*vx > 0 && pl.on_ground) *vx -= SKID;
        else if (*vx > -max) *vx -= acc;
        pl.facing = 1;
    } else if (pl.on_ground) {
        *vx = *vx > FRICTION ? *vx - FRICTION : *vx < -FRICTION ? *vx + FRICTION : 0;
    }
    if (pl.on_ground && *vx > max) *vx -= 2;           // 走るのをやめたら、少しずつ遅く
    if (pl.on_ground && *vx < -max) *vx += 2;
    if (jump && !pl.jump_held && pl.on_ground) {         // 押した瞬間だけ跳ぶ。走っていると高く
        pl.b.vy = -(JUMP_V + iabs(*vx) / 4);
        pl.on_ground = 0;
    }
    pl.jump_held = jump;
    if (fire && !pl.fire_held && pl.power == 2) shoot();
    pl.fire_held = fire;
    pl.b.vy += pl.b.vy < 0 && jump ? GRAV : GRAV_FAST;   // 早く離すと小さく跳ぶ
    if (pl.b.vy > MAX_FALL) pl.b.vy = MAX_FALL;

    int r = body_move(&pl.b, 1);
    pl.on_ground = (r & ON_GROUND) != 0;
    if (r & HIT_U) bump_tile(bump_c, bump_r);
    if ((pl.b.x >> 4) < G.cam) {                         // 画面の左端より左へは戻れない
        pl.b.x = G.cam << 4;
        if (*vx < 0) *vx = 0;
    }
    // リフトに乗る（上から来たときだけ）
    pl.lift = 0;
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing *t = &th[i];
        if ((t->type != T_LIFT && t->type != T_LIFT_V) || pl.b.vy < 0) continue;
        int px = pl.b.x >> 4, foot = (pl.b.y >> 4) + pl.b.h, lx = t->b.x >> 4, ly = t->b.y >> 4;
        if (px + pl.b.w > lx && px < lx + t->b.w && foot >= ly && foot <= ly + 6 + (pl.b.vy >> 4)) {
            pl.b.y = (ly - pl.b.h) << 4;
            pl.b.vy = 0;
            pl.on_ground = 1;
            pl.lift = i + 1;
        }
    }
    pl.anim += iabs(*vx);
    if (pl.hurt) pl.hurt--;
    if (pl.star) pl.star--;
    if (pl.grow) pl.grow--;
    touch_tiles();
    if ((pl.b.y >> 4) >= ROWS * TILE) {                  // 穴に落ちた
        if (G.god) pl.b.vy = -160;
        else kill_player();
    }
    if (st.axe && G.mode == M_PLAY && (pl.b.x >> 4) + pl.b.w > st.axe * TILE) G.mode = M_AXE;   // 斧を跳び越えても
    if (st.goal && G.mode == M_PLAY && (pl.b.x >> 4) + pl.b.w >= st.goal * TILE + 6) {
        // ゴールの棒をつかむ。高いほど点が多い
        int y = pl.b.y >> 4;
        add_score(y < 40 ? 5000 : y < 72 ? 2000 : y < 104 ? 800 : y < 128 ? 400 : 100);
        pl.b.x = (st.goal * TILE + 7 - pl.b.w) << 4;
        if (y < 16) pl.b.y = 16 << 4;
        pl.b.vx = pl.b.vy = 0;
        G.mode = M_POLE;
    }
}

// ---- 敵・アイテムの動き ----

static void fall(Thing *t, int g) {
    t->b.vy += g;
    if (t->b.vy > MAX_FALL) t->b.vy = MAX_FALL;
}

static int is_walker(int type) { return type == T_WALKER || type == T_BEETLE || type == T_SPIKY; }

// 歩く。壁や、ほかの歩く敵に当たったら向きを変える
static int walk(Thing *t, int speed) {
    t->b.vx = t->dir * speed;
    fall(t, GRAV);
    int r = body_move(&t->b, 0);
    if (r & (HIT_L | HIT_R)) t->dir = -t->dir;
    if (is_walker(t->type) && !t->state)
        for (int i = 0; i < MAX_THINGS; i++) {
            Thing *u = &th[i];
            if (u == t || !is_walker(u->type) || u->state || !overlap(&t->b, &u->b)) continue;
            t->dir = t->b.x < u->b.x ? -1 : 1;
            u->dir = -t->dir;
        }
    t->anim++;
    return r;
}

// 出てくる場所に画面が近づいたら出す
static void spawn_pending(void) {
    while (next_spawn < nspawns && spawns[next_spawn].col * TILE < G.cam + VIEW_W + 16) {
        struct spawn *s = &spawns[next_spawn++];
        int x = s->col * TILE, y = s->row * TILE;
        if (x < G.cam - 32) continue;       // 途中から始めたとき、後ろの敵は出さない
        Thing *t;
        switch (s->type) {
        case 'g': new_thing(T_WALKER, x, y); break;
        case 'b': new_thing(T_BEETLE, x, y); break;
        case 'p': new_thing(T_FLYER, x, y); break;
        case 's': new_thing(T_SPIKY, x, y); break;
        case 'h':
            t = new_thing(T_THROWER, x, y);
            t->timer = 80;
            t->c = 60;
            t->a = -1;
            break;
        case 'f':                           // 土管の口の中（隠れている）
            t = new_thing(T_PLANT, x + 8, y);
            t->a = y;
            t->behind = 1;
            t->timer = 60;
            break;
        case 'K':
            t = new_thing(T_CANNON, x, y);
            t->timer = 50;
            break;
        case 'r': case 'R':
            t = new_thing(T_FIREBAR, x, y);
            t->c = s->type == 'r' ? 6 : 11;
            t->dir = s->type == 'r' ? 1 : -1;
            break;
        case 'v':
            t = new_thing(T_BUBBLE, x, ROWS * TILE + 8);
            t->timer = 20 + (s->col * 37) % 80;
            break;
        case 'Z':
            t = new_thing(T_BOSS, x, y - TILE);
            t->hp = st.boss_hp;
            t->timer = 60;
            t->a = -1;
            break;
        case 'e':
            t = new_thing(T_LIFT, x, y);
            t->b.vx = 12;
            break;
        case 'E':
            t = new_thing(T_LIFT_V, x, y);
            t->b.vy = 12;
            break;
        }
    }
}

static void boss_update(Thing *t) {
    t->anim++;
    if (t->c > 0) t->c--;
    if (t->hp <= 0) {                       // 倒れた: 逆さまに落ちる
        t->b.y += t->b.vy;
        fall(t, GRAV);
        return;
    }
    fall(t, GRAV);
    if (G.mode == M_AXE) {                  // 橋が落ちていく: 動かずに、足もとが無くなったら落ちる
        t->b.vx = 0;
        body_move(&t->b, 0);
        return;
    }
    t->dir = pl.b.x > t->b.x ? 1 : -1;
    int x = t->b.x >> 4;
    if (--t->timer <= 0) {
        t->a = -t->a;
        t->timer = 50 + (G.frame * 7) % 80;
    }
    if (x < t->x0 - 64) t->a = 1;
    if (x > t->x0 + 8) t->a = -1;
    t->b.vx = t->a * 6;
    int r = body_move(&t->b, 0);
    if ((r & ON_GROUND) && G.frame % 170 == 0) t->b.vy = -90;
    int world = G.stage / 4;
    if (++t->state >= 150 - world * 10) {   // 火を吐く。高さはロボットに合わせていく
        t->state = 0;
        int fx = t->dir < 0 ? (t->b.x >> 4) - 18 : (t->b.x >> 4) + t->b.w + 2;
        Thing *f = new_thing(T_BOSSFIRE, fx, (t->b.y >> 4) + 2);
        f->dir = t->dir;
        f->b.vx = t->dir * 24;
        f->a = (pl.b.y >> 4) + (pl.b.h > 16 ? 12 : 4);
        if (world >= 4 && t->hp > 0) {      // 後半のワールドはレンチも投げる
            Thing *w = new_thing(T_WRENCH, x, (t->b.y >> 4) - 8);
            w->dir = t->dir;
            w->b.vx = t->dir * 18;
            w->b.vy = -110;
        }
    }
}

static void item_update(Thing *t) {
    if (t->state == 0) {                    // 箱から出てくる
        t->b.y -= 16;
        if (--t->timer <= 0) {
            t->state = 1;
            t->behind = 0;
        }
        return;
    }
    t->anim++;
    if (t->type == T_BLASTER) {
        fall(t, GRAV);
        body_move(&t->b, 0);
    } else if (t->type == T_STAR) {
        t->b.vx = t->dir * 20;
        fall(t, 5);
        int r = body_move(&t->b, 0);
        if (r & (HIT_L | HIT_R)) t->dir = -t->dir;
        if (r & ON_GROUND) t->b.vy = -90;
    } else {
        walk(t, 16);
    }
}

void things_update(void) {
    spawn_pending();
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing *t = &th[i];
        switch (t->type) {
        case T_NONE:
            continue;
        case T_WALKER:
            if (t->state) {                 // 踏まれてつぶれている
                if (--t->timer <= 0) t->type = T_NONE;
            } else {
                walk(t, 8);
            }
            break;
        case T_BEETLE: case T_SPIKY:
            walk(t, 8);
            break;
        case T_SHELL:
            if (t->c > 0) t->c--;
            if (t->state == 0) {            // 止まっている。しばらくすると起き上がる
                t->b.vx = 0;
                fall(t, GRAV);
                body_move(&t->b, 0);
                if (--t->timer <= 0) {
                    t->type = T_BEETLE;
                    t->dir = pl.b.x > t->b.x ? 1 : -1;
                }
            } else {                        // 滑っている: 当たった敵を倒す
                walk(t, 64);
                for (int j = 0; j < MAX_THINGS; j++) {
                    Thing *u = &th[j];
                    if (u != t && is_foe(u->type) && overlap(&t->b, &u->b)) knock_out(u, 200);
                }
            }
            break;
        case T_FLYER: {
            t->b.vx = t->dir * 8;
            fall(t, 4);
            int r = body_move(&t->b, 0);
            if (r & (HIT_L | HIT_R)) t->dir = -t->dir;
            if (r & ON_GROUND) t->b.vy = -84;
            t->anim++;
            break;
        }
        case T_PLANT: {
            t->anim++;
            int top = t->a - 24;
            switch (t->state) {
            case 0:                         // 隠れている。ロボットが土管のそばにいるときは出ない
                if (--t->timer <= 0) {
                    if (iabs((pl.b.x >> 4) + 5 - (t->b.x >> 4) - 5) > 28) t->state = 1;
                    else t->timer = 20;
                }
                break;
            case 1:
                t->b.y -= 16;
                if ((t->b.y >> 4) <= top) {
                    t->b.y = top << 4;
                    t->state = 2;
                    t->timer = 60;
                }
                break;
            case 2:
                if (--t->timer <= 0) t->state = 3;
                break;
            case 3:
                t->b.y += 16;
                if ((t->b.y >> 4) >= t->a) {
                    t->b.y = t->a << 4;
                    t->state = 0;
                    t->timer = 90;
                }
                break;
            }
            break;
        }
        case T_CANNON: {
            int sx = (t->b.x >> 4) - G.cam, d = (pl.b.x >> 4) - (t->b.x >> 4);
            if (--t->timer <= 0) {
                t->timer = 110 + (G.frame * 13) % 100;
                if (sx > -16 && sx < VIEW_W && iabs(d) > 40) {
                    int dir = d > 0 ? 1 : -1;
                    Thing *b = new_thing(T_BULLET, (t->b.x >> 4) + dir * 12, t->b.y >> 4);
                    b->dir = dir;
                    b->b.vx = dir * 40;
                }
            }
            break;
        }
        case T_BULLET:
            t->b.x += t->b.vx;
            break;
        case T_THROWER: {
            t->anim++;
            t->dir = pl.b.x > t->b.x ? 1 : -1;
            int x = t->b.x >> 4;
            if (x < t->x0 - 16) t->a = 1;
            if (x > t->x0 + 16) t->a = -1;
            t->b.vx = t->a * 6;
            fall(t, GRAV);
            int r = body_move(&t->b, 0);
            if (r & (HIT_L | HIT_R)) t->a = -t->a;
            if ((r & ON_GROUND) && --t->timer <= 0) {
                t->b.vy = -100;
                t->timer = 150 + G.frame % 60;
            }
            if (--t->c <= 0) {
                t->c = 70 + G.frame % 40;
                Thing *w = new_thing(T_WRENCH, x - 2, (t->b.y >> 4) - 12);
                w->dir = t->dir;
                w->b.vx = t->dir * 22;
                w->b.vy = -100;
            }
            break;
        }
        case T_WRENCH: case T_DEBRIS: case T_DEAD:
            t->b.x += t->b.vx;
            t->b.y += t->b.vy;
            fall(t, t->type == T_WRENCH ? 5 : GRAV);
            t->anim++;
            break;
        case T_FIREBAR:
            t->a += t->dir * 5;
            break;
        case T_BUBBLE:
            if (t->state == 0) {
                if (--t->timer <= 0) {
                    t->state = 1;
                    t->b.vy = -150;
                }
            } else {
                t->b.y += t->b.vy;
                t->b.vy += 5;
                if (t->b.vy > 0 && (t->b.y >> 4) >= t->y0 + t->dy) {
                    t->state = 0;
                    t->b.y = (t->y0 + t->dy) << 4;
                    t->b.vy = 0;
                    t->timer = 90 + G.frame % 60;
                }
            }
            break;
        case T_BOSS:
            boss_update(t);
            break;
        case T_BOSSFIRE: {
            t->b.x += t->b.vx;
            int y = t->b.y >> 4;
            if (y < t->a) t->b.y += 8;
            if (y > t->a) t->b.y -= 8;
            t->anim++;
            break;
        }
        case T_LIFT: {
            int x = t->b.x >> 4;
            if (x < t->x0 - 40) t->b.vx = 12;
            if (x > t->x0 + 40) t->b.vx = -12;
            t->b.x += t->b.vx;
            if (pl.lift == i + 1) pl.b.x += t->b.vx;
            break;
        }
        case T_LIFT_V: {
            int y = t->b.y >> 4;
            if (y < t->y0 - 48) t->b.vy = 12;
            if (y > t->y0 + 48) t->b.vy = -12;
            t->b.y += t->b.vy;
            if (pl.lift == i + 1) pl.b.y += t->b.vy;
            break;
        }
        case T_BATTERY: case T_BLASTER: case T_STAR: case T_ONEUP:
            item_update(t);
            break;
        case T_COINPOP:
            t->b.y += t->b.vy;
            t->b.vy += 10;
            t->anim++;
            if (--t->timer <= 0) t->type = T_NONE;
            break;
        case T_SHOT: {
            t->b.vx = t->dir * 80;
            fall(t, 8);
            int r = body_move(&t->b, 0);
            if (r & (HIT_L | HIT_R)) t->type = T_NONE;
            else if (r & ON_GROUND) t->b.vy = -56;
            t->anim++;
            int sx = (t->b.x >> 4) - G.cam;
            if (sx < -16 || sx > VIEW_W + 16) t->type = T_NONE;
            break;
        }
        case T_FLAG:
            if (G.mode == M_POLE && (t->b.y >> 4) < 8 * TILE) t->b.y += 32;
            break;
        }
        // 画面から遠く離れた・下に落ちたものは消す
        int sx = (t->b.x >> 4) - G.cam;
        if (t->type != T_FLAG && (sx < -64 || sx > VIEW_W + 160 || (t->b.y >> 4) > ROWS * TILE + 48)) t->type = T_NONE;
    }
}

// ---- ロボットとの当たり ----

static int stomping(Thing *t) {
    return pl.b.vy > 0 && (pl.b.y >> 4) + pl.b.h - (t->b.y >> 4) < 10;
}

static void touch_enemy(Thing *t) {
    if (pl.star) {
        if (t->type != T_BOSS) knock_out(t, 200);
        return;
    }
    int can_stomp = t->type == T_WALKER || t->type == T_BEETLE || t->type == T_FLYER || t->type == T_BULLET ||
                    t->type == T_THROWER;
    if (!can_stomp || !stomping(t)) {
        hurt_player();
        return;
    }
    pl.b.vy = pl.jump_held ? -STOMP_V_HELD : -STOMP_V;
    add_score(100);
    switch (t->type) {
    case T_WALKER:
        t->state = 1;
        t->timer = 30;
        break;
    case T_BEETLE:
        t->type = T_SHELL;
        t->state = 0;
        t->timer = 400;
        break;
    case T_FLYER:
        t->type = T_BEETLE;
        break;
    default:
        knock_out(t, 100);
        break;
    }
}

static void touch_shell(Thing *t) {
    if (t->c > 0) return;                   // 蹴った直後
    if (pl.star) {
        knock_out(t, 200);
        return;
    }
    int stomp = stomping(t);
    if (t->state == 0) {                    // 止まっている: 蹴る
        t->state = 1;
        t->dir = (pl.b.x >> 4) + 5 < (t->b.x >> 4) + 6 ? 1 : -1;
        t->c = 10;
        add_score(100);
        if (stomp) pl.b.vy = -STOMP_V;
    } else if (stomp) {                     // 滑っている甲羅を踏むと止まる
        t->state = 0;
        t->timer = 400;
        pl.b.vy = -STOMP_V;
        add_score(100);
    } else {
        hurt_player();
    }
}

static const short quarter_sin[65] = {
    0, 6, 13, 19, 25, 31, 38, 44, 50, 56, 62, 68, 74, 80, 86, 92, 98, 104, 109, 115, 121, 126, 132, 137, 142, 147,
    152, 157, 162, 167, 172, 177, 181, 185, 190, 194, 198, 202, 206, 209, 213, 216, 220, 223, 226, 229, 231, 234,
    237, 239, 241, 243, 245, 247, 248, 250, 251, 252, 253, 254, 255, 255, 256, 256, 256,
};

// 角度 a（1 周 256）の sin × 256
static int sin256(int a) {
    a &= 255;
    if (a < 64) return quarter_sin[a];
    if (a < 128) return quarter_sin[128 - a];
    if (a < 192) return -quarter_sin[a - 128];
    return -quarter_sin[256 - a];
}

// ファイアバーの i 番目の玉の中心（点）
static void firebar_ball(Thing *t, int i, int *x, int *y) {
    int a = t->a >> 2;
    *x = t->x0 + 8 + sin256(a + 64) * i * 8 / 256;
    *y = t->y0 + 8 + sin256(a) * i * 8 / 256;
}

static void pick_item(Thing *t) {
    switch (t->type) {
    case T_BATTERY:
        if (!pl.power) pl.power = 1;
        break;
    case T_BLASTER:
        pl.power = pl.power ? 2 : 1;
        break;
    case T_STAR:
        pl.star = 500;
        break;
    case T_ONEUP:
        G.lives++;
        break;
    }
    player_size();
    add_score(1000);
    t->type = T_NONE;
}

static void shot_hits(Thing *s) {
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing *t = &th[i];
        if (t == s || !overlap(&s->b, &t->b)) continue;
        if (t->type == T_PLANT && t->a - (t->b.y >> 4) <= 4) continue;
        if (is_foe(t->type)) {
            knock_out(t, 200);
        } else if (t->type == T_BOSS && t->hp > 0) {
            t->c = 16;
            if (--t->hp <= 0) {
                t->b.vy = -40;
                add_score(5000);
            }
        } else if (t->type != T_BULLET) {
            continue;                       // 弾は砲台の弾に当たると消える
        }
        s->type = T_NONE;
        return;
    }
}

void interact(void) {
    if (G.mode != M_PLAY) return;
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing *t = &th[i];
        switch (t->type) {
        case T_WALKER:
            if (t->state) break;
            /* fallthrough */
        case T_BEETLE: case T_FLYER: case T_SPIKY: case T_BULLET: case T_THROWER:
            if (overlap(&pl.b, &t->b)) touch_enemy(t);
            break;
        case T_BOSS:
            if (t->hp > 0 && overlap(&pl.b, &t->b)) touch_enemy(t);
            break;
        case T_SHELL:
            if (overlap(&pl.b, &t->b)) touch_shell(t);
            break;
        case T_PLANT: {
            Body v = t->b;                  // 土管から出ている分だけ
            v.h = t->a - (t->b.y >> 4);
            if (v.h > 4 && overlap(&pl.b, &v)) touch_enemy(t);
            break;
        }
        case T_WRENCH: case T_BUBBLE: case T_BOSSFIRE:
            if (overlap(&pl.b, &t->b)) hurt_player();
            break;
        case T_FIREBAR:
            for (int k = 0; k < t->c; k++) {
                int x, y;
                firebar_ball(t, k, &x, &y);
                Body ball = { (x - 3) << 4, (y - 3) << 4, 0, 0, 6, 6 };
                if (overlap(&pl.b, &ball)) hurt_player();
            }
            break;
        case T_BATTERY: case T_BLASTER: case T_STAR: case T_ONEUP:
            if (t->state && overlap(&pl.b, &t->b)) pick_item(t);
            break;
        case T_SHOT:
            shot_hits(t);
            break;
        }
    }
}

// ---- 描く ----

// 敵の絵は左向き。右へ動いているときは反転する
static const char *thing_pic(Thing *t, int *flip) {
    int right = t->dir > 0, step = (t->anim >> 3) & 1;
    *flip = 0;
    switch (t->type) {
    case T_WALKER: *flip = right; return t->state ? pic_walker_flat : step ? pic_walker2 : pic_walker;
    case T_BEETLE: *flip = right; return (t->anim >> 4) & 1 ? pic_beetle2 : pic_beetle1;
    case T_SHELL: return pic_shell;
    case T_FLYER: *flip = right; return pic_beetle1;
    case T_PLANT: return (t->anim >> 4) & 1 ? pic_plant_open : pic_plant_closed;
    case T_SPIKY: *flip = right; return step ? pic_spiky2 : pic_spiky1;
    case T_BULLET: *flip = right; return pic_bullet;
    case T_THROWER: *flip = right; return t->c < 16 ? pic_thrower2 : pic_thrower;   // 投げる前に振りかぶる
    case T_WRENCH: *flip = (t->anim >> 3) & 1 ? 3 : 0; return (t->anim >> 2) & 1 ? pic_wrench : pic_wrench_v;
    case T_BUBBLE: *flip = (t->b.vy > 0 ? 2 : 0) | ((t->anim >> 2) & 1); return pic_bubble;
    case T_BOSSFIRE: *flip = right | ((t->anim >> 2) & 1 ? 2 : 0); return pic_bossfire;
    case T_BATTERY: return pic_battery;
    case T_BLASTER: return pic_blaster;
    case T_STAR: return (t->anim >> 2) & 1 ? pic_star2 : pic_star;
    case T_ONEUP: return pic_oneup;
    case T_COINPOP: return pic_coin_frames[(t->anim >> 1) & 3];
    case T_SHOT: *flip = (t->anim >> 2) & 3; return pic_shot;
    case T_DEBRIS: *flip = (t->anim >> 2) & 3; return pic_debris;
    case T_FLAG: return pic_flag;
    case T_DEAD: *flip = t->flip; return t->pic;
    }
    return 0;
}

void draw_things(int behind) {
    for (int i = 0; i < MAX_THINGS; i++) {
        Thing *t = &th[i];
        if (!t->type || t->behind != behind) continue;
        int x = (t->b.x >> 4) - t->dx - G.cam, y = (t->b.y >> 4) - t->dy + TOP;
        switch (t->type) {
        case T_CANNON:
            break;
        case T_PLANT: {
            int flip;
            draw_sprite(x, y, thing_pic(t, &flip), 0);
            draw_sprite(x, y + TILE, pic_stem, 0);
            break;
        }
        case T_FLYER:
            draw_sprite(x, y, pic_beetle1, t->dir > 0);
            draw_sprite(x, y - 3, (t->anim >> 2) & 1 ? pic_wing2 : pic_wing1, t->dir > 0);
            break;
        case T_FIREBAR:
            for (int k = 0; k < t->c; k++) {
                int bx, by;
                firebar_ball(t, k, &bx, &by);
                draw_sprite(bx - 8 - G.cam, by - 8 + TOP, pic_fireball, 0);
            }
            break;
        case T_BOSS: {
            if (t->c & 2) break;                    // 撃たれて点滅
            int f = t->dir > 0, v = t->hp <= 0 ? 2 : 0;
            int top = v ? TILE : 0, bot = v ? 0 : TILE;
            char (*b)[257] = pic_boss[t->state < 20 || v];   // 火を吐いた直後と、倒れたときは口を開ける
            y += (t->anim >> 4) & 1;                // 歩くと少し上下する
            draw_sprite(x + (f ? TILE : 0), y + top, b[0], f | v);
            draw_sprite(x + (f ? 0 : TILE), y + top, b[1], f | v);
            draw_sprite(x + (f ? TILE : 0), y + bot, b[2], f | v);
            draw_sprite(x + (f ? 0 : TILE), y + bot, b[3], f | v);
            break;
        }
        case T_LIFT: case T_LIFT_V:
            for (int k = 0; k < 3; k++) draw_sprite(x + k * TILE, y, pic_lift, 0);
            break;
        default: {
            int flip;
            const char *p = thing_pic(t, &flip);
            if (p) draw_sprite(x, y, p, flip);
        }
        }
    }
}

// ロボット: 立っているとときどき瞬きする。歩くと 2 枚を交互に、足をそろえた絵のときは 1 点浮く（弾む）
void draw_player(void) {
    if (pl.hidden || (pl.hurt && (G.frame & 2))) return;
    int set = pl.star ? 2 + ((G.frame >> 2) & 1) : pl.power == 2 ? 1 : 0;
    int big = pl.power > 0;
    if (pl.grow && (pl.grow & 4)) big = !big;       // 大きさが変わる途中は、大小を交互に
    int x = (pl.b.x >> 4) - 3 - G.cam, foot = (pl.b.y >> 4) + pl.b.h + TOP;
    if (G.mode == M_DYING) {
        draw_sprite(x, foot - TILE, pic_robot[0][R_DEAD], 0);
        return;
    }
    int moving = pl.b.vx != 0, step = (pl.anim >> 7) & 1, blink = G.frame % 170 < 7;
    int bob = pl.on_ground && moving && !step ? -1 : 0;
    const char **pic = pic_robot[set];
    if (!big) {
        int k = !pl.on_ground ? R_JUMP : moving && step ? R_WALK : blink ? R_BLINK : R_STAND;
        draw_sprite(x, foot - TILE + bob, pic[k], pl.facing);
    } else {
        int k = !pl.on_ground ? R_BJUMP : moving && step ? R_BWALK : R_BSTAND;
        draw_sprite(x, foot - 2 * TILE + bob, pic[blink ? R_BBLINK : R_BTOP], pl.facing);
        draw_sprite(x, foot - TILE + bob, pic[k], pl.facing);
    }
}
