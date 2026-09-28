// ROBO JUMP: 横スクロールのアクション。ロボットが 8 ワールド × 4 ステージを進み、城のボスを倒して金の歯車を目指す。
// このファイルは流れ（タイトル、ステージの紹介、遊ぶ、ゴール、ゲームオーバー、エンディング）と、ステージ・文字の描画
#include "robo.h"

struct game G;
extern const unsigned char font[];  // font.s: 8 × 8 のフォント（0x20〜0x7e）

enum { R_QUIT, R_CLEAR, R_MISS };
enum { SKY = RGB6(2, 4, 5), NIGHT = RGB6(0, 0, 1) };

// ---- 絵 ----

#define PIC(name) extern const char name[]
PIC(pic_robot_stand); PIC(pic_robot_walk); PIC(pic_robot_jump); PIC(pic_robot_dead);
PIC(pic_robot_btop); PIC(pic_robot_bstand); PIC(pic_robot_bwalk1); PIC(pic_robot_bjump);
PIC(pic_robot_blink); PIC(pic_robot_bblink);
PIC(pic_grass); PIC(pic_dirt); PIC(pic_brick); PIC(pic_question); PIC(pic_used); PIC(pic_hard); PIC(pic_stone);
PIC(pic_pipe_tl); PIC(pic_pipe_tr); PIC(pic_pipe_l); PIC(pic_pipe_r); PIC(pic_tree_l); PIC(pic_tree_m); PIC(pic_trunk);
PIC(pic_bridge); PIC(pic_axe); PIC(pic_lava_top); PIC(pic_lava); PIC(pic_cannon_top); PIC(pic_cannon_base);
PIC(pic_coin1); PIC(pic_coin2); PIC(pic_coin3); PIC(pic_cloud); PIC(pic_bush); PIC(pic_hill_top); PIC(pic_hill_l);
PIC(pic_hill_m); PIC(pic_pole); PIC(pic_pole_top); PIC(pic_crenel); PIC(pic_door_top); PIC(pic_black);
PIC(pic_boss32); PIC(pic_boss32_open);
#undef PIC

const char *pic_robot[4][10];
const char *pic_coin_frames[4];
char pic_boss[2][4][257];
char pic_shot[257], pic_oneup[257], pic_wrench_v[257], pic_star2[257];
static char robot_buf[4][10][257];
static char pic_q2[257], pic_q3[257];                       // ? の箱が光る
static char pic_stone_under[257], pic_brick_under[257], pic_hard_under[257];

// from の文字を to の同じ位置の文字に置き換えて写す（色違いの絵）
static void recolor(char *dst, const char *src, const char *from, const char *to) {
    for (int i = 0; i < 256; i++) {
        char c = src[i];
        for (int k = 0; from[k]; k++)
            if (c == from[k]) {
                c = to[k];
                break;
            }
        dst[i] = c;
    }
    dst[256] = 0;
}

static void init_pics(void) {
    const char *robot[10] = { pic_robot_stand, pic_robot_walk, pic_robot_jump, pic_robot_dead, pic_robot_btop,
                              pic_robot_bstand, pic_robot_bwalk1, pic_robot_bjump, pic_robot_blink, pic_robot_bblink };
    static const char *body[4] = { "lmu", "pcq", "ye6", "tai" };   // 体の青を: ふつう、ブラスター（赤）、スター（黄・緑）
    for (int s = 0; s < 4; s++)
        for (int k = 0; k < 10; k++) {
            recolor(robot_buf[s][k], robot[k], "lmu", body[s]);
            pic_robot[s][k] = robot_buf[s][k];
        }
    pic_coin_frames[0] = pic_coin1;
    pic_coin_frames[1] = pic_coin2;
    pic_coin_frames[2] = pic_coin3;
    pic_coin_frames[3] = pic_coin2;
    for (int m = 0; m < 2; m++)                             // 32 × 32 のボスを 4 つに分ける
        for (int p = 0; p < 4; p++)
            for (int r = 0; r < 16; r++)
                for (int c = 0; c < 16; c++)
                    pic_boss[m][p][r * 16 + c] = (m ? pic_boss32_open : pic_boss32)[(r + (p >> 1) * 16) * 32 + c + (p & 1) * 16];
    recolor(pic_shot, pic_fireball, "ce", "9b");            // ロボットの弾は青
    recolor(pic_oneup, pic_battery, "tiv", "pdj");          // 1UP はピンクの電池
    recolor(pic_star2, pic_star, "ye", "fy");               // スターが瞬く
    for (int r = 0; r < 16; r++)                            // 縦のレンチ（縦横を入れ替える）
        for (int c = 0; c < 16; c++) pic_wrench_v[r * 16 + c] = pic_wrench[c * 16 + r];
    recolor(pic_q2, pic_question, "o", "e");
    recolor(pic_q3, pic_question, "o", "6");
    recolor(pic_stone_under, pic_stone, "78", "91");        // 地下は青
    recolor(pic_brick_under, pic_brick, "r", "1");
    recolor(pic_hard_under, pic_hard, "onk", "931");
}

// ---- 文字 ----

static void center(int row, const char *s, int fg, int bg) {
    int n = 0;
    while (s[n]) n++;
    text_color(fg, bg);
    text_goto((40 - n) / 2, row);
    text_write(s);
}

// フォントを scale 倍にして描く（タイトルの大きな文字）。影つき
static void big_text(int x, int y, const char *s, int scale, int color) {
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; s[i]; i++) {
            const unsigned char *g = &font[(s[i] - 32) * 8];
            for (int r = 0; r < 8; r++)
                for (int c = 0; c < 8; c++)
                    if (g[r] & (0x80 >> c))
                        fill_rect(x + i * 8 * scale + c * scale + (pass ? 0 : 2), y + r * scale + (pass ? 0 : 2), scale,
                                  scale, pass ? color : 0);
        }
}

// v を digits 桁（0 で埋める）で p に書き、書き終えた次を返す
static char *put_num(char *p, int v, int digits) {
    for (int i = digits - 1; i >= 0; i--, v /= 10) p[i] = '0' + v % 10;
    return p + digits;
}

static char *put_str(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}

static char *put_stage(char *p, int n) {
    if (n < 0) return put_str(p, "TEST");
    *p++ = '1' + n / 4;
    *p++ = '-';
    *p++ = '1' + n % 4;
    return p;
}

// 1 行目: 点数、コイン、ワールド、時間
static void draw_hud(void) {
    char s[41], *p = s;
    fill_rect(0, 0, VIEW_W, TOP, 0);
    p = put_num(p, G.score, 6);
    p = put_str(p, "  $x");
    p = put_num(p, G.coins, 2);
    p = put_str(p, "  WORLD ");
    p = put_stage(p, G.stage);
    if (G.state == S_PLAY) {
        p = put_str(p, "  TIME ");
        p = put_num(p, G.time, 3);
    }
    *p = 0;
    text_color(15, 0);
    text_goto(1, 0);
    text_write(s);
}

// ---- 1 コマ ----

static unsigned long next_tick;
static unsigned char key_char;      // このコマに来た文字（矢印や Enter）

// 次のコマの時刻まで待つ。Esc が来たら 1
static int frame_wait(void) {
    while (ticks < next_tick) halt();
    next_tick += FRAME;
    if (ticks > next_tick + 20) next_tick = ticks;          // 大きく遅れたら追いかけない
    key_char = 0;
    unsigned char c;
    while ((c = getc_nowait())) {
        if (c == KEY_ESC) return 1;
        key_char = c;
    }
    G.frame++;
    return 0;
}

static void show(void) {
    draw_hud();
    wait_vsync();
    present();
}

// n コマ、画面をそのままにして待つ。Esc なら 1。enter_ok なら Enter でも抜ける
static int hold(int n, int enter_ok) {
    for (int i = 0; i < n; i++) {
        if (frame_wait()) return 1;
        if (enter_ok && key_char == '\n') return 0;
    }
    return 0;
}

// ---- ステージを描く ----

static const char *question_pic(void) {
    static const int seq[8] = { 0, 0, 0, 0, 1, 2, 1, 0 };
    int k = seq[(G.frame >> 3) & 7];
    return k == 0 ? pic_question : k == 1 ? pic_q2 : pic_q3;
}

static const char *tile_pic(int c, int r, int *flip) {
    char ch = map[r][c], up = r > 0 ? map[r - 1][c] : '.', left = c > 0 ? map[r][c - 1] : '.';
    char right = c + 1 < MAX_COLS ? map[r][c + 1] : '.';
    int th = st.theme;
    *flip = 0;
    switch (ch) {
    case '#':
        if (th == TH_UNDER) return pic_stone_under;
        if (th == TH_CASTLE) return pic_stone;
        return up != '#' ? pic_grass : pic_dirt;
    case 'B': case '$': case 'S': return th == TH_UNDER ? pic_brick_under : pic_brick;
    case '?': case 'M': return question_pic();
    case 'U': return pic_used;
    case 'H': return th == TH_UNDER ? pic_hard_under : pic_hard;
    case '[': return pic_pipe_tl;
    case ']': return pic_pipe_tr;
    case '{': return pic_pipe_l;
    case '}': return pic_pipe_r;
    case 't':
        if (left != 't') return pic_tree_l;
        if (right != 't') {
            *flip = 1;
            return pic_tree_l;
        }
        return pic_tree_m;
    case 'i': return pic_trunk;
    case 'W': return pic_stone;
    case '=': return pic_bridge;
    case 'A': return pic_axe;
    case '~': return up != '~' ? pic_lava_top : pic_lava;
    case 'K': return pic_cannon_top;
    case 'k': return pic_cannon_base;
    case 'o': return pic_coin_frames[(G.frame >> 3) & 3];
    case '(': return pic_cloud;
    case ')': *flip = 1; return pic_cloud;
    case 'y': *flip = left == 'y'; return pic_bush;
    case '^': return pic_hill_top;
    case '/': return pic_hill_l;
    case '\\': *flip = 1; return pic_hill_l;
    case 'n': return pic_hill_m;
    case '|': return pic_pole;
    case 'O': return pic_pole_top;
    case 'x': return pic_brick;
    case 'm': return pic_crenel;
    case 'd': return pic_black;
    case 'D': return up != 'D' ? pic_door_top : pic_black;
    }
    return 0;
}

static void draw_world(void) {
    static const int bounce[9] = { 0, 1, 3, 5, 6, 6, 5, 3, 1 };
    int bg = st.theme == TH_UNDER || st.theme == TH_CASTLE ? 0 : st.night ? NIGHT : SKY;
    fill_rect(0, TOP, VIEW_W, ROWS * TILE, bg);
    draw_things(1);                                         // 土管の花、箱から出てくるもの（マスの後ろ）
    int c0 = G.cam >> 4;
    for (int c = c0; c <= c0 + VIEW_W / TILE && c < st.width; c++)
        for (int r = 0; r < ROWS; r++) {
            int flip;
            const char *p = tile_pic(c, r, &flip);
            if (!p) continue;
            int y = TOP + r * TILE;
            if (G.bounce_t && c == G.bounce_c && r == G.bounce_r) y -= bounce[G.bounce_t];
            draw_sprite(c * TILE - G.cam, y, p, flip);
        }
    if (G.bounce_t) G.bounce_t--;
    draw_things(0);
    draw_player();
}

static void scroll(void) {
    int target = (pl.b.x >> 4) - 136, maxc = st.width * TILE - VIEW_W;
    if (target > G.cam) G.cam = target;                     // 前にだけ進む
    if (G.cam > maxc) G.cam = maxc;
    if (G.cam < 0) G.cam = 0;
}

// テストが読むので、毎コマ写しておく
static void mirror(void) {
    G.power = pl.power;
    G.px = pl.b.x >> 4;
    G.py = pl.b.y >> 4;
}

// ---- 場面 ----

static int intro(void) {
    char s[20], *p = put_str(s, "WORLD ");
    *put_stage(p, G.stage) = 0;
    fill_rect(0, 0, VIEW_W, SCREEN_H, 0);
    center(9, s, 15, 0);
    draw_sprite(136, 104, pic_robot[0][R_STAND], 0);
    p = put_str(s, "x ");
    *put_num(p, G.lives, G.lives >= 10 ? 2 : 1) = 0;
    text_goto(20, 14);
    text_write(s);
    show();
    return hold(100, 0);
}

static int pause_game(void) {
    center(12, " PAUSE ", 15, 1);
    show();
    for (;;) {
        if (frame_wait()) return 1;
        if (key_char == '\n') return 0;
    }
}

// 城の最後: 橋を斧の側から 1 マスずつ落とす。ボスは足もとが無くなると溶岩へ
static int bridge_step(int timer) {
    if (timer == 1) map[8][st.axe] = '.';
    if (timer % 3) return 1;
    for (int c = st.axe - 1; c >= 0; c--)
        if (map[9][c] == '=') {
            map[9][c] = '.';
            return 1;
        }
    return 0;
}

static int play_stage(void) {
    build_stage(G.stage);
    actors_reset();
    int col = G.checkpoint_reached && st.checkpoint ? st.checkpoint : 2;
    player_place(col);
    G.cam = 0;
    G.cam = col * TILE - 48 > 0 ? col * TILE - 48 : 0;
    scroll();
    G.time = st.time;
    G.mode = M_PLAY;
    if (st.goal) new_thing(T_FLAG, st.goal * TILE - 8, TILE);
    int tsub = 0, timer = 0, pit = 0;
    for (;;) {
        if (frame_wait()) return R_QUIT;
        if (key_char == '\n' && G.mode == M_PLAY && pause_game()) return R_QUIT;
        switch (G.mode) {
        case M_PLAY:
            things_update();
            player_update();
            interact();
            scroll();
            if (++tsub >= 20) {                             // 時間は 0.4 秒に 1 減る
                tsub = 0;
                if (G.time > 0 && --G.time == 0) kill_player();
            }
            if (st.checkpoint && (pl.b.x >> 4) >= st.checkpoint * TILE) G.checkpoint_reached = 1;
            if (G.mode == M_DYING) {
                timer = 0;
                pit = (pl.b.y >> 4) >= ROWS * TILE;
            }
            if (G.mode == M_AXE) timer = 0;
            break;
        case M_DYING:                                       // 止まって、跳ね上がって、下へ落ちる（穴なら待つだけ）
            timer++;
            if (!pit && timer == 30) pl.b.vy = -130;
            if (!pit && timer > 30) {
                pl.b.y += pl.b.vy;
                pl.b.vy += 6;
            }
            if (timer > 150) return R_MISS;
            break;
        case M_POLE:                                        // 棒を滑り降りる。旗も下りる
            things_update();
            if ((pl.b.y >> 4) + pl.b.h < 9 * TILE) {
                pl.b.y += 32;
            } else {
                pl.b.y = (9 * TILE - pl.b.h) << 4;
                if (++timer > 40) {
                    G.mode = M_WALK;
                    pl.facing = 0;
                    timer = 0;
                }
            }
            break;
        case M_WALK:                                        // 城まで歩いて入る
            pl.b.vx = 20;
            pl.b.vy += 6;
            pl.on_ground = (body_move(&pl.b, 1) & ON_GROUND) != 0;
            pl.anim += 20;
            scroll();
            if ((pl.b.x >> 4) >= (st.goal + 6) * TILE + 3) {
                pl.hidden = 1;
                G.mode = M_BONUS;
                timer = 0;
            }
            break;
        case M_BONUS:                                       // 残った時間を点数に
            if (G.time > 0) {
                int n = G.time >= 3 ? 3 : G.time;
                G.time -= n;
                add_score(50 * n);
            } else if (++timer > 60) {
                return R_CLEAR;
            }
            break;
        case M_AXE:                                         // ロボットはその場で待つ（宙にいれば下りる）
            pl.b.vx = 0;
            pl.b.vy += 6;
            pl.on_ground = (body_move(&pl.b, 1) & ON_GROUND) != 0;
            things_update();
            timer++;
            if (!bridge_step(timer) && timer > 200) G.mode = M_BONUS, timer = 0;
            break;
        }
        mirror();
        draw_world();
        if (G.mode == M_AXE && timer > 120) center(6, G.stage == STAGES - 1 ? "THE ROBO KING FELL!" : "THE CASTLE IS CLEAR!", 15, 0);
        show();
    }
}

static int game_over(void) {
    fill_rect(0, 0, VIEW_W, SCREEN_H, 0);
    center(11, "GAME OVER", 15, 0);
    show();
    return hold(200, 1);
}

static int ending(void) {
    static const char *lines[] = {
        "THE ROBO KING HAS FALLEN.",
        "",
        "YOU FOUND THE GOLDEN GEAR!",
        "",
        "THANK YOU FOR PLAYING",
        "ROBO JUMP",
    };
    fill_rect(0, 0, VIEW_W, SCREEN_H, 0);
    for (int i = 0; i < 6; i++) center(6 + i * 2, lines[i], i == 5 ? 14 : 15, 0);
    draw_sprite(152, 150, pic_robot[2][R_STAND], 0);
    show();
    if (hold(150, 0)) return 1;
    center(22, "PRESS ENTER", 7, 0);
    show();
    for (;;) {
        if (frame_wait()) return 1;
        if (key_char == '\n') return 0;
    }
}

// タイトル: 左右でワールドを選び、Enter で始める。Esc なら -1
static int title(int *sel) {
    build_stage(0);
    for (int r = 0; r < 10; r++)                            // 背景は 1-1 の初めの景色。箱や土管は文字と重なるので消す
        for (int c = 0; c < 21; c++)
            if (tile_solid(map[r][c])) map[r][c] = '.';
    actors_reset();
    pl.power = 0;
    player_place(3);
    G.cam = 0;
    G.state = S_TITLE;
    for (;;) {
        if (frame_wait()) return -1;
        if (key_char == KEY_LEFT) *sel = (*sel + STAGES - 1) % STAGES;
        if (key_char == KEY_RIGHT) *sel = (*sel + 1) % STAGES;
        if (key_char == KEY_UP) *sel = (*sel + STAGES - 4) % STAGES;
        if (key_char == KEY_DOWN) *sel = (*sel + 4) % STAGES;
        if (key_char == '\n') return *sel;
        draw_world();
        big_text(96, 20, "ROBO", 4, 15);
        big_text(96, 56, "JUMP", 4, 14);
        char s[40], *p = put_str(s, "<  WORLD ");
        p = put_stage(p, *sel);
        *put_str(p, "  >") = 0;
        center(13, s, 15, 1);
        center(15, "ENTER: START", 15, 1);
        center(17, "ARROWS: MOVE  Z: JUMP  X: RUN/FIRE", 15, 1);
        center(18, "ENTER: PAUSE  ESC: QUIT", 15, 1);
        fill_rect(0, 0, VIEW_W, TOP, 0);
        wait_vsync();
        present();
    }
}

// 1 回のゲーム（ステージを進む）。ゲームオーバー・エンディングなら 0、Esc なら 1
static int run_game(void) {
    pl.power = 0;
    for (;;) {
        G.state = S_INTRO;
        if (intro()) return 1;
        G.state = S_PLAY;
        int r = play_stage();
        if (r == R_QUIT) return 1;
        if (r == R_CLEAR) {
            G.checkpoint_reached = 0;
            if (G.stage == STAGES - 1) {
                G.state = S_ENDING;
                return ending();
            }
            if (G.stage < 0) return 0;
            G.stage++;
        } else if (--G.lives <= 0) {
            G.state = S_GAMEOVER;
            return game_over();
        }
    }
}

// シェルの jump コマンド。arg: 空ならタイトルから。"3-2" ならそのステージから、"test" はテスト用のステージ。
// 後ろに god を付けると、ミスしない（試すとき）
void jump_main(const char *arg) {
    int start = -2, sel = 0;
    G = (struct game){0};
    if (arg[0] >= '1' && arg[0] <= '8' && arg[1] == '-' && arg[2] >= '1' && arg[2] <= '4') start = (arg[0] - '1') * 4 + arg[2] - '1';
    if (arg[0] == 't' && arg[1] == 'e' && arg[2] == 's' && arg[3] == 't') start = -1;
    for (const char *p = arg; *p; p++)
        if (p[0] == 'g' && p[1] == 'o' && p[2] == 'd') G.god = 1;
    text_cursor(0);
    init_pics();
    next_tick = ticks;
    for (;;) {
        if (start == -2) {
            int s = title(&sel);
            if (s < 0) break;
            G.stage = s;
        } else {
            G.stage = start;
            start = -2;
        }
        G.lives = 3;
        G.coins = G.score = 0;
        G.checkpoint_reached = 0;
        if (run_game()) break;
    }
    while (getc_nowait()) {}
    text_cursor(1);
    text_color(7, 1);                                       // シェルの色に戻して消す
    con_clear();
}
