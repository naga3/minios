// ROBO JUMP: 横スクロールのアクション。robo.c（流れと描画）、actors.c（ロボットと、敵・アイテム）で共有するもの
#pragma once
#include "tiny.h"
#include "stage.h"

enum { TOP = 8, VIEW_W = 320, FRAME = 2, MAX_THINGS = 64 };

// 位置と速さは 1/16 点の単位
typedef struct {
    int x, y, vx, vy;       // 当たりの四角の左上と速さ
    int w, h;               // 当たりの四角の大きさ（点）
} Body;

enum { HIT_L = 1, HIT_R = 2, HIT_U = 4, ON_GROUND = 8 };
int body_move(Body *b, int is_player);

// 敵・アイテム・弾など
enum {
    T_NONE, T_WALKER, T_BEETLE, T_SHELL, T_FLYER, T_PLANT, T_SPIKY, T_CANNON, T_BULLET,
    T_THROWER, T_WRENCH, T_FIREBAR, T_BUBBLE, T_BOSS, T_BOSSFIRE, T_LIFT, T_LIFT_V,
    T_BATTERY, T_BLASTER, T_STAR, T_ONEUP, T_COINPOP, T_SHOT, T_DEBRIS, T_FLAG, T_DEAD,
};

typedef struct {
    Body b;
    int type;
    int dx, dy;             // 絵の左上 = 当たりの左上 - (dx, dy)
    int state, timer, dir, anim, hp;
    int x0, y0;             // 出てきた位置（点）
    int a, c;               // 種類ごとの値
    int behind;             // マスより先に描く（土管の花、箱から出てくるもの）
    const char *pic;        // T_DEAD の絵
    int flip;
} Thing;

typedef struct {
    Body b;
    int power;              // 0 小さい、1 大きい、2 ブラスター
    int facing;             // 0 右、1 左
    int on_ground, jump_held, fire_held;
    int anim;
    int hurt;               // 当たっても平気な残りのコマ（点滅）
    int star;               // スターの残りのコマ
    int grow;               // 大きさが変わる途中（点滅）
    int lift;               // 乗っているリフトの番号 + 1
    int hidden;             // 城に入った
} Player;

// 場面（テストがメモリから読むので、先頭の並びを変えない）
enum { S_TITLE, S_INTRO, S_PLAY, S_GAMEOVER, S_ENDING };
enum { M_PLAY, M_DYING, M_POLE, M_WALK, M_BONUS, M_AXE, M_DONE };
extern struct game {
    int state, stage, lives, coins, score, time, power, cam, px, py, mode;
    int god;                // 試すとき: ミスしない
    int frame;
    int checkpoint_reached;
    int bounce_c, bounce_r, bounce_t;   // 叩かれて跳ねているマス
} G;

extern Player pl;
extern Thing th[MAX_THINGS];

// actors.c
void actors_reset(void);
void player_place(int col);
void things_update(void);
void player_update(void);
void interact(void);
void draw_things(int behind);
void draw_player(void);
void hurt_player(void);
void kill_player(void);
void add_score(int n);
void add_coin(void);
Thing *new_thing(int type, int x, int y);

// robo.c が用意する絵
extern const char *pic_robot[4][10];    // [色の組][絵]（0 ふつう、1 ブラスター、2・3 スター）
enum { R_STAND, R_WALK, R_JUMP, R_DEAD, R_BTOP, R_BSTAND, R_BWALK1, R_BWALK2, R_BJUMP, R_WALK2 };
extern const char *pic_coin_frames[4];
extern char pic_boss[4][257];
extern char pic_shot[257], pic_oneup[257], pic_wrench_v[257];

// pics.c
#define PIC(name) extern const char name[]
PIC(pic_walker); PIC(pic_walker_flat); PIC(pic_beetle1); PIC(pic_beetle2); PIC(pic_shell);
PIC(pic_wing1); PIC(pic_wing2); PIC(pic_plant_open); PIC(pic_plant_closed); PIC(pic_stem);
PIC(pic_spiky1); PIC(pic_spiky2); PIC(pic_bullet); PIC(pic_thrower); PIC(pic_wrench);
PIC(pic_fireball); PIC(pic_bubble); PIC(pic_bossfire); PIC(pic_lift);
PIC(pic_battery); PIC(pic_blaster); PIC(pic_star); PIC(pic_debris); PIC(pic_flag);
#undef PIC
