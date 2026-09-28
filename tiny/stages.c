// ROBO JUMP の 32 のステージ（8 ワールド × 4）。ワールドの 1 は地上、2 は地下、3 は空（木の上とリフト）、4 は城（ボス）。
// 地図は下の小さな関数（地面、土管、階段など）を並べて作る。形はすべてオリジナル
#include "stage.h"

char map[ROWS][MAX_COLS];
struct spawn spawns[MAX_SPAWNS];
int nspawns;
struct stage st;

int tile_solid(char c) {
    switch (c) {
    case '#': case 'B': case '?': case 'M': case '$': case 'S': case 'U': case 'H':
    case '[': case ']': case '{': case '}': case 't': case 'W': case '=': case 'K': case 'k':
        return 1;
    }
    return 0;
}

// ---- 部品 ----

static void begin(int theme, int width, int time) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++) map[r][c] = '.';
    nspawns = 0;
    st = (struct stage){ .theme = theme, .width = width, .time = time };
}

static void set(int c, int r, char ch) {
    if (c >= 0 && c < MAX_COLS && r >= 0 && r < ROWS) map[r][c] = ch;
}

static char get(int c, int r) {
    return c >= 0 && c < MAX_COLS && r >= 0 && r < ROWS ? map[r][c] : '#';
}

// 四角く埋める（両端を含む）
static void fill(int c0, int r0, int c1, int r1, char ch) {
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++) set(c, r, ch);
}

static void ground(int c0, int c1) { fill(c0, 10, c1, 11, '#'); }

// 高い地面（top の行から下を埋める）
static void plateau(int c0, int c1, int top) { fill(c0, top, c1, 11, '#'); }

// 横に並べる（空白は飛ばす）
static void put(int c, int r, const char *s) {
    for (; *s; s++, c++)
        if (*s != ' ') set(c, r, *s);
}

static void enemy(int c, int r, char type) {
    if (nspawns < MAX_SPAWNS) spawns[nspawns++] = (struct spawn){ c, r, type };
}

// 地面の上（9 行目）に、gap マスおきに n 匹
static void walkers(int c, char type, int n, int gap) {
    for (int i = 0; i < n; i++) enemy(c + i * gap, 9, type);
}

// 土管: 地面から h マスの高さ。胴は一番下まで
static void pipe(int c, int h) {
    int top = 10 - h;
    set(c, top, '[');
    set(c + 1, top, ']');
    fill(c, top + 1, c, 11, '{');
    fill(c + 1, top + 1, c + 1, 11, '}');
}

static void plant_pipe(int c, int h) {
    pipe(c, h);
    enemy(c, 10 - h, 'f');
}

// 階段: n 列。up なら右へ上る（高さ 1, 2, ... n）、そうでなければ下る。9 行目の上に積む
static void stairs(int c, int n, int up) {
    for (int i = 0; i < n; i++) {
        int h = up ? i + 1 : n - i;
        fill(c + i, 10 - h, c + i, 9, 'H');
    }
}

static void tower(int c, int h) { fill(c, 10 - h, c, 9, 'H'); }

static void coins(int c, int r, int n) {
    for (int i = 0; i < n; i++) set(c + i, r, 'o');
}

// 木の上（空のステージの足場）: r 行目に幅 w。下に幹
static void tree(int c, int r, int w) {
    for (int i = 0; i < w; i++) set(c + i, r, 't');
    for (int rr = r + 1; rr < ROWS; rr++)
        for (int i = 1; i < w - 1; i++)
            if (get(c + i, rr) == '.') set(c + i, rr, 'i');
}

static void castle(int c) {
    static const char *rows[] = { " mmm ", " xdx ", "mmmmm", "xxDxx", "xxDxx" };
    for (int r = 0; r < 5; r++)
        for (int i = 0; i < 5; i++)
            if (rows[r][i] != ' ' && get(c + i, 5 + r) == '.') set(c + i, 5 + r, rows[r][i]);
}

// ゴール: c 列に棒、4 列右に城。地面はここから右の端まで
static void goal(int c) {
    ground(c - 1, c + 14);
    set(c, 1, 'O');
    fill(c, 2, c, 8, '|');
    set(c, 9, 'H');
    castle(c + 4);
    st.goal = c;
    st.width = c + 15;
}

static void lava(int c0, int c1) { fill(c0, 10, c1, 11, '~'); }

// 城の最後: c0〜c1 に溶岩の上の橋、その右に斧、ボス。斧の先に床
static void boss_bridge(int c0, int c1, int hp) {
    fill(c0, 9, c1, 9, '=');
    lava(c0, c1);
    set(c1 + 1, 8, 'A');
    fill(c1 + 1, 9, c1 + 16, 11, 'W');
    fill(c1 + 1, 0, c1 + 16, 3, 'W');
    enemy(c1 - 3, 8, 'Z');
    st.axe = c1 + 1;
    st.boss_hp = hp;
    st.width = c1 + 17;
}

// 地下の天井（0 行目のれんが）
static void ceiling(int c0, int c1) { fill(c0, 0, c1, 0, 'B'); }

// 城の床（10〜11 行目）と天井（0〜rows 行目）
static void floor_w(int c0, int c1) { fill(c0, 10, c1, 11, 'W'); }
static void roof(int c0, int c1, int rows) { fill(c0, 0, c1, rows, 'W'); }

// ファイアバー: 使った箱の上で回る
static void firebar(int c, int r, int longer) {
    set(c, r, 'U');
    enemy(c, r, longer ? 'R' : 'r');
}

// 溶岩の穴と、跳ねる火の玉
static void lava_pit(int c0, int c1, int bubble) {
    lava(c0, c1);
    if (bubble) enemy((c0 + c1) / 2, 11, 'v');
}

// 砲台: 地面から h マス（頭 1 と台 h-1）
static void cannon(int c, int h) {
    set(c, 10 - h, 'K');
    fill(c, 11 - h, c, 9, 'k');
    enemy(c, 10 - h, 'K');
}

// ---- 飾り（雲、丘、草むら）: 空いている所に、ステージごとに決まった並びで置く ----

static unsigned seed;
static int rnd(int n) {
    seed = seed * 1103515245 + 12345;
    return (seed >> 16) % n;
}

static int empty(int c, int r) { return get(c, r) == '.'; }

static void decorate(int n) {
    seed = 7 + n * 131;
    if (st.theme == TH_OVER || st.theme == TH_SKY)
        for (int c = 2 + rnd(6); c < st.width - 3; c += 9 + rnd(12)) {
            if (st.goal && c >= st.goal - 1 && c <= st.goal + 9) continue;
            int r = 1 + rnd(3);
            if (empty(c, r) && empty(c + 1, r)) put(c, r, "()");
        }
    if (st.theme != TH_OVER) return;
    int end = st.goal ? st.goal - 3 : st.width - 4;
    for (int c = 1 + rnd(4); c < end; c += 5 + rnd(9)) {
        int ok = 1;
        for (int i = 0; i < 3; i++)
            if (get(c + i, 10) != '#' || !empty(c + i, 9) || !empty(c + i, 8)) ok = 0;
        if (!ok) continue;
        if (rnd(2)) {
            put(c, 9, "/n\\");
            set(c + 1, 8, '^');
        } else {
            put(c, 9, "yy");
        }
    }
}

// ---- ステージ ----

// テスト用: 真上にパワーアップの箱（出てきた電池は右の段で折り返して戻ってくる）、コインの箱、歩く敵、近くにゴール
static void stage_test(void) {
    begin(TH_OVER, 40, 400);
    ground(0, 40);
    put(2, 6, "M");
    tower(6, 1);
    put(9, 6, "?B$");
    walkers(16, 'g', 1, 0);
    goal(26);
}

// ---- ワールド 1: 歩く敵と甲羅の虫。土管、階段 ----

static void s1_1(void) {
    begin(TH_OVER, 0, 400);
    ground(0, 62);
    put(12, 6, "?");
    put(17, 6, "B?BMB");
    put(19, 2, "?");
    walkers(22, 'g', 1, 0);
    pipe(27, 2);
    walkers(33, 'g', 1, 0);
    pipe(37, 3);
    put(43, 6, "BBB");
    coins(43, 5, 3);
    walkers(46, 'g', 2, 2);
    pipe(52, 3);
    ground(65, 100);
    put(67, 6, "B$B");
    put(70, 6, "BB");
    put(72, 2, "BBSBBB");
    walkers(75, 'b', 1, 0);
    walkers(84, 'g', 2, 2);
    stairs(97, 4, 1);
    ground(104, 150);
    stairs(104, 4, 0);
    put(112, 6, "?B?");
    put(113, 2, "M");
    walkers(115, 'g', 3, 2);
    pipe(124, 2);
    put(130, 6, "BBB?BBB");
    put(133, 2, "l");
    walkers(134, 'b', 1, 0);
    coins(140, 7, 4);
    ground(154, 200);
    walkers(158, 'g', 2, 2);
    pipe(164, 2);
    stairs(170, 5, 1);
    tower(175, 5);
    goal(184);
    st.checkpoint = 92;
}

static void s1_2(void) {
    begin(TH_UNDER, 0, 400);
    ground(0, 44);
    ceiling(6, 158);
    put(10, 6, "??M??");
    walkers(16, 'g', 2, 2);
    fill(22, 7, 23, 9, 'B');
    put(27, 5, "BBBBBB");
    coins(27, 4, 6);
    walkers(30, 'b', 1, 0);
    plant_pipe(37, 3);
    ground(48, 104);
    plateau(50, 54, 8);
    put(58, 6, "BB$BB");
    walkers(60, 'g', 3, 2);
    plant_pipe(68, 2);
    put(73, 3, "BBBBBBBB");
    coins(73, 2, 8);
    put(73, 6, "B      B");
    walkers(76, 'b', 2, 3);
    stairs(96, 4, 1);
    ground(108, 180);
    stairs(108, 4, 0);
    put(116, 6, "S");
    walkers(119, 'g', 4, 2);
    plant_pipe(128, 3);
    plant_pipe(135, 4);
    put(141, 6, "l");
    fill(146, 3, 151, 3, 'B');
    put(146, 7, "BBBBBB");
    coins(146, 9, 6);
    walkers(150, 'b', 1, 0);
    goal(166);
    st.checkpoint = 88;
}

static void s1_3(void) {
    begin(TH_SKY, 0, 300);
    ground(0, 12);
    tree(15, 9, 4);
    tree(21, 7, 5);
    enemy(23, 6, 'b');
    coins(21, 4, 5);
    tree(29, 9, 3);
    tree(34, 6, 6);
    put(36, 2, "M");
    enemy(37, 5, 'g');
    tree(43, 8, 4);
    enemy(47, 5, 'e');
    tree(53, 7, 5);
    enemy(55, 6, 'b');
    tree(61, 9, 3);
    tree(66, 5, 4);
    coins(66, 2, 4);
    tree(73, 8, 6);
    enemy(75, 7, 'p');
    enemy(82, 6, 'E');
    tree(87, 7, 4);
    tree(94, 9, 7);
    enemy(97, 8, 'g');
    enemy(99, 8, 'g');
    tree(104, 6, 3);
    put(105, 2, "S");
    tree(110, 8, 5);
    enemy(116, 6, 'e');
    tree(122, 7, 4);
    enemy(123, 6, 'p');
    tree(129, 9, 3);
    tree(134, 6, 5);
    coins(134, 4, 5);
    tree(141, 8, 4);
    ground(148, 175);
    goal(158);
    st.checkpoint = 75;
}

static void s1_4(void) {
    begin(TH_CASTLE, 0, 300);
    floor_w(0, 22);
    roof(0, 120, 1);
    lava_pit(23, 25, 1);
    floor_w(26, 44);
    firebar(33, 6, 0);
    fill(38, 2, 42, 5, 'W');
    lava_pit(45, 48, 1);
    floor_w(49, 64);
    fill(52, 8, 54, 9, 'W');
    firebar(58, 5, 0);
    fill(62, 7, 64, 9, 'W');
    lava_pit(65, 68, 1);
    floor_w(69, 87);
    firebar(74, 7, 0);
    put(77, 5, "M");
    firebar(81, 7, 0);
    boss_bridge(88, 100, 3);
    st.checkpoint = 49;
}

// ---- ワールド 2: 土管の花と、羽の甲羅の虫 ----

static void s2_1(void) {
    begin(TH_OVER, 0, 400);
    ground(0, 50);
    put(9, 6, "?M?");
    walkers(15, 'g', 3, 2);
    plant_pipe(24, 3);
    put(30, 6, "B?B?B");
    put(32, 2, "B$B");
    walkers(33, 'b', 1, 0);
    plant_pipe(40, 2);
    walkers(45, 'p', 1, 0);
    ground(54, 90);
    plateau(56, 60, 8);
    enemy(58, 7, 'g');
    plateau(64, 66, 6);
    put(64, 2, "BSB");
    walkers(70, 'b', 2, 3);
    pipe(78, 4);
    walkers(83, 'p', 1, 0);
    stairs(87, 3, 1);
    ground(95, 130);
    put(100, 6, "BBBBBB");
    put(102, 2, "???");
    walkers(101, 'g', 4, 2);
    plant_pipe(112, 3);
    plant_pipe(118, 4);
    walkers(122, 'b', 1, 0);
    ground(134, 190);
    put(138, 6, "l");
    walkers(142, 'g', 2, 2);
    walkers(148, 'p', 1, 0);
    put(150, 6, "B?B");
    pipe(156, 2);
    stairs(162, 6, 1);
    tower(168, 6);
    goal(178);
    st.checkpoint = 96;
}

static void s2_2(void) {
    begin(TH_UNDER, 0, 400);
    ground(0, 36);
    ceiling(5, 154);
    put(8, 6, "?M?");
    walkers(13, 'b', 2, 3);
    fill(20, 6, 24, 9, 'B');
    fill(25, 5, 26, 9, 'B');
    coins(25, 4, 2);
    plant_pipe(32, 2);
    ground(40, 92);
    walkers(42, 'g', 3, 2);
    put(50, 6, "BBBB$BBBB");
    put(50, 2, "BBBBBBBBB");
    enemy(52, 5, 'b');
    plant_pipe(64, 3);
    walkers(70, 'p', 2, 3);
    plateau(76, 80, 7);
    put(76, 3, "BBSBB");
    plant_pipe(86, 3);
    ground(96, 130);
    walkers(98, 'g', 4, 2);
    fill(108, 7, 115, 7, 'B');
    coins(108, 9, 8);
    walkers(110, 'b', 1, 0);
    plant_pipe(120, 2);
    plant_pipe(126, 4);
    ground(135, 175);
    put(140, 6, "l");
    walkers(144, 'b', 2, 3);
    stairs(150, 4, 1);
    goal(162);
    st.checkpoint = 98;
}

static void s2_3(void) {
    begin(TH_SKY, 0, 300);
    ground(0, 10);
    tree(13, 8, 3);
    tree(19, 6, 4);
    enemy(20, 5, 'p');
    tree(26, 9, 5);
    enemy(28, 8, 'b');
    enemy(34, 7, 'e');
    tree(41, 7, 3);
    put(42, 3, "M");
    tree(47, 5, 4);
    coins(47, 2, 4);
    tree(54, 8, 6);
    enemy(56, 7, 'g');
    enemy(58, 7, 'g');
    enemy(63, 5, 'E');
    tree(68, 6, 3);
    tree(74, 9, 4);
    enemy(76, 6, 'p');
    tree(81, 7, 5);
    enemy(83, 6, 'b');
    enemy(89, 7, 'e');
    tree(96, 8, 3);
    tree(101, 6, 4);
    put(102, 2, "S");
    tree(108, 9, 3);
    tree(113, 7, 3);
    enemy(113, 4, 'p');
    enemy(119, 6, 'E');
    tree(124, 8, 5);
    enemy(126, 7, 'b');
    tree(132, 6, 4);
    coins(132, 3, 4);
    tree(139, 8, 3);
    ground(145, 175);
    goal(156);
    st.checkpoint = 81;
}

static void s2_4(void) {
    begin(TH_CASTLE, 0, 300);
    floor_w(0, 18);
    roof(0, 130, 1);
    lava_pit(19, 22, 1);
    floor_w(23, 40);
    firebar(28, 6, 0);
    firebar(35, 6, 0);
    lava_pit(41, 43, 1);
    fill(44, 8, 47, 11, 'W');
    lava_pit(48, 50, 1);
    floor_w(51, 70);
    fill(51, 2, 70, 5, 'W');
    firebar(60, 9, 0);
    lava_pit(71, 74, 1);
    floor_w(75, 97);
    put(80, 6, "M");
    firebar(86, 6, 1);
    boss_bridge(98, 110, 4);
    st.checkpoint = 51;
}

// ---- ワールド 3: 夜。トゲの虫 ----

static void s3_1(void) {
    begin(TH_OVER, 0, 400);
    st.night = 1;
    ground(0, 40);
    put(10, 6, "M");
    walkers(14, 'b', 2, 3);
    pipe(22, 2);
    walkers(26, 's', 1, 0);
    plant_pipe(30, 3);
    walkers(35, 'p', 2, 3);
    ground(45, 85);
    put(48, 6, "B?B?B");
    put(50, 2, "S");
    walkers(52, 's', 2, 3);
    plateau(60, 64, 7);
    enemy(61, 6, 'b');
    plant_pipe(70, 4);
    walkers(76, 'g', 2, 2);
    stairs(82, 4, 1);
    ground(90, 140);
    stairs(90, 4, 0);
    put(100, 6, "BB$BB");
    walkers(101, 'p', 2, 3);
    plant_pipe(110, 3);
    walkers(115, 's', 3, 2);
    put(122, 6, "?M?");
    walkers(126, 'b', 2, 3);
    ground(145, 200);
    put(150, 6, "l");
    walkers(153, 'g', 4, 2);
    plant_pipe(164, 2);
    stairs(170, 7, 1);
    tower(177, 7);
    goal(186);
    st.checkpoint = 95;
}

static void s3_2(void) {
    begin(TH_UNDER, 0, 400);
    ground(0, 30);
    ceiling(4, 160);
    put(8, 6, "?M?");
    walkers(12, 's', 2, 3);
    plant_pipe(20, 3);
    fill(26, 1, 28, 7, 'B');
    ground(34, 70);
    walkers(36, 'b', 3, 2);
    put(42, 6, "BBBBBBBBBB");
    put(42, 3, "B$BBSBBBBB");
    walkers(44, 'g', 2, 3);
    plant_pipe(56, 2);
    plant_pipe(62, 3);
    walkers(66, 'p', 1, 0);
    ground(75, 120);
    plateau(78, 81, 8);
    plateau(82, 85, 6);
    enemy(83, 5, 's');
    walkers(90, 'b', 3, 3);
    fill(100, 7, 107, 7, 'B');
    coins(100, 6, 8);
    walkers(103, 'g', 2, 2);
    plant_pipe(112, 4);
    ground(125, 180);
    put(130, 6, "l");
    walkers(133, 's', 3, 3);
    plant_pipe(145, 3);
    walkers(150, 'b', 2, 3);
    stairs(156, 4, 1);
    goal(168);
    st.checkpoint = 90;
}

static void s3_3(void) {
    begin(TH_SKY, 0, 300);
    st.night = 1;
    ground(0, 9);
    tree(12, 7, 3);
    enemy(17, 6, 'e');
    tree(24, 8, 4);
    enemy(25, 7, 's');
    tree(31, 5, 3);
    put(32, 1, "M");
    tree(37, 8, 5);
    enemy(39, 6, 'p');
    enemy(46, 6, 'E');
    tree(52, 7, 3);
    tree(57, 9, 6);
    enemy(59, 8, 'b');
    enemy(61, 8, 'b');
    enemy(66, 7, 'e');
    tree(74, 6, 4);
    coins(74, 3, 4);
    tree(81, 8, 3);
    enemy(81, 5, 'p');
    tree(87, 6, 3);
    enemy(93, 5, 'E');
    tree(99, 8, 5);
    enemy(101, 7, 's');
    tree(107, 6, 3);
    put(108, 2, "S");
    enemy(113, 6, 'e');
    tree(122, 7, 4);
    enemy(122, 4, 'p');
    tree(129, 9, 3);
    tree(134, 6, 3);
    tree(140, 8, 4);
    ground(147, 175);
    goal(157);
    st.checkpoint = 74;
}

static void s3_4(void) {
    begin(TH_CASTLE, 0, 300);
    floor_w(0, 16);
    roof(0, 140, 1);
    lava_pit(17, 19, 1);
    floor_w(20, 30);
    firebar(24, 7, 0);
    fill(28, 8, 30, 9, 'W');
    lava_pit(31, 36, 1);
    fill(33, 8, 34, 11, 'W');
    floor_w(37, 60);
    fill(40, 2, 60, 4, 'W');
    firebar(44, 9, 0);
    firebar(52, 9, 0);
    lava_pit(61, 64, 1);
    fill(65, 7, 67, 11, 'W');
    firebar(66, 3, 0);
    lava_pit(68, 71, 1);
    floor_w(72, 101);
    put(76, 6, "M");
    firebar(84, 6, 1);
    enemy(90, 9, 's');
    firebar(94, 6, 0);
    boss_bridge(102, 114, 5);
    st.checkpoint = 37;
}

// ---- ワールド 4: 砲台 ----

static void s4_1(void) {
    begin(TH_OVER, 0, 400);
    ground(0, 50);
    put(9, 6, "?M?");
    cannon(16, 2);
    walkers(20, 'b', 2, 3);
    plant_pipe(28, 3);
    cannon(34, 3);
    walkers(38, 'g', 3, 2);
    put(44, 6, "BBBB");
    put(44, 2, "B$BB");
    ground(55, 100);
    cannon(58, 2);
    walkers(62, 's', 2, 3);
    plateau(68, 72, 7);
    cannon(70, 5);
    walkers(76, 'p', 2, 3);
    plant_pipe(84, 4);
    walkers(89, 'b', 2, 2);
    stairs(97, 4, 1);
    ground(105, 150);
    stairs(105, 4, 0);
    put(115, 6, "B?BSB?B");
    cannon(124, 2);
    walkers(128, 'g', 4, 2);
    cannon(138, 3);
    walkers(142, 'p', 1, 0);
    ground(155, 200);
    put(160, 6, "l");
    walkers(163, 's', 2, 3);
    cannon(170, 2);
    stairs(174, 6, 1);
    tower(180, 6);
    goal(188);
    st.checkpoint = 110;
}

static void s4_2(void) {
    begin(TH_UNDER, 0, 400);
    ground(0, 28);
    ceiling(4, 158);
    put(8, 6, "M");
    walkers(12, 's', 3, 3);
    plant_pipe(24, 2);
    ground(33, 60);
    fill(36, 6, 37, 9, 'B');
    fill(42, 4, 43, 9, 'B');
    fill(48, 6, 49, 9, 'B');
    walkers(39, 'g', 1, 0);
    walkers(45, 'g', 1, 0);
    coins(42, 3, 2);
    plant_pipe(54, 3);
    ground(65, 110);
    walkers(67, 'b', 3, 2);
    put(72, 6, "BBB$BBB");
    put(72, 3, "BBBMBBB");
    walkers(80, 'p', 2, 3);
    plant_pipe(90, 4);
    plant_pipe(96, 2);
    walkers(100, 's', 2, 3);
    ground(115, 170);
    plateau(118, 121, 8);
    plateau(122, 125, 6);
    plateau(126, 129, 8);
    enemy(123, 5, 'b');
    put(123, 2, "S");
    walkers(133, 'g', 4, 2);
    put(140, 6, "l");
    plant_pipe(146, 3);
    stairs(152, 4, 1);
    goal(164);
    st.checkpoint = 86;
}

static void s4_3(void) {
    begin(TH_SKY, 0, 300);
    ground(0, 8);
    enemy(11, 8, 'e');
    tree(18, 6, 3);
    enemy(19, 3, 'p');
    enemy(24, 5, 'E');
    tree(30, 8, 4);
    enemy(31, 7, 's');
    enemy(37, 6, 'e');
    tree(44, 5, 3);
    put(45, 1, "M");
    tree(50, 8, 3);
    enemy(56, 7, 'E');
    tree(62, 6, 5);
    enemy(63, 5, 'b');
    enemy(65, 5, 'b');
    enemy(70, 8, 'e');
    tree(77, 7, 3);
    enemy(78, 3, 'p');
    enemy(83, 6, 'E');
    tree(89, 8, 4);
    coins(89, 5, 4);
    enemy(96, 7, 'e');
    tree(103, 6, 3);
    put(104, 2, "S");
    enemy(109, 5, 'E');
    tree(115, 7, 5);
    enemy(117, 6, 's');
    enemy(123, 6, 'e');
    tree(130, 8, 3);
    tree(135, 6, 3);
    enemy(135, 3, 'p');
    tree(141, 8, 4);
    ground(148, 178);
    goal(158);
    st.checkpoint = 62;
}

static void s4_4(void) {
    begin(TH_CASTLE, 0, 300);
    floor_w(0, 14);
    roof(0, 150, 1);
    lava_pit(15, 18, 1);
    floor_w(19, 36);
    firebar(23, 6, 1);
    fill(30, 7, 36, 9, 'W');
    firebar(33, 3, 0);
    lava_pit(37, 40, 1);
    fill(41, 7, 44, 11, 'W');
    lava_pit(45, 48, 1);
    floor_w(49, 72);
    fill(49, 2, 72, 5, 'W');
    firebar(55, 9, 0);
    firebar(63, 9, 0);
    enemy(68, 9, 'g');
    lava_pit(73, 78, 0);
    fill(75, 8, 76, 11, 'W');
    floor_w(79, 105);
    put(84, 6, "M");
    firebar(90, 6, 1);
    cannon(97, 2);
    boss_bridge(106, 118, 6);
    st.checkpoint = 49;
}

// ---- ワールド 5: レンチ投げ ----

static void s5_1(void) {
    begin(TH_OVER, 0, 400);
    ground(0, 44);
    put(10, 6, "?M?");
    walkers(15, 'b', 3, 2);
    cannon(24, 2);
    walkers(28, 'h', 1, 0);
    plant_pipe(34, 3);
    walkers(38, 'g', 2, 2);
    ground(49, 90);
    put(52, 6, "BBBBB");
    put(52, 2, "BB$BB");
    walkers(55, 'h', 1, 0);
    cannon(62, 3);
    walkers(66, 's', 3, 2);
    plateau(74, 78, 7);
    enemy(76, 6, 'h');
    plant_pipe(84, 4);
    ground(95, 150);
    walkers(97, 'p', 3, 3);
    put(106, 6, "B?BSB?B");
    walkers(108, 'b', 2, 3);
    cannon(118, 2);
    cannon(124, 3);
    walkers(128, 'h', 1, 0);
    walkers(134, 'g', 4, 2);
    ground(155, 205);
    put(158, 6, "l");
    walkers(162, 's', 2, 3);
    enemy(168, 9, 'h');
    stairs(174, 7, 1);
    tower(181, 7);
    goal(190);
    st.checkpoint = 97;
}

static void s5_2(void) {
    begin(TH_UNDER, 0, 400);
    ground(0, 26);
    ceiling(4, 160);
    put(8, 6, "M");
    walkers(12, 'b', 2, 3);
    plant_pipe(20, 4);
    ground(31, 70);
    walkers(33, 'h', 1, 0);
    put(38, 6, "BBBBBBBB");
    put(38, 3, "BBSBBBBB");
    walkers(40, 's', 3, 2);
    plant_pipe(50, 2);
    plant_pipe(56, 3);
    plant_pipe(62, 4);
    ground(75, 115);
    plateau(76, 80, 8);
    enemy(78, 7, 'h');
    fill(84, 5, 90, 5, 'B');
    coins(84, 4, 7);
    walkers(86, 'b', 3, 2);
    plant_pipe(100, 3);
    walkers(106, 'p', 2, 3);
    ground(120, 175);
    put(124, 6, "B$BMB");
    walkers(128, 'h', 1, 0);
    walkers(134, 's', 4, 2);
    put(146, 6, "l");
    plant_pipe(150, 3);
    stairs(156, 4, 1);
    goal(168);
    st.checkpoint = 94;
}

static void s5_3(void) {
    begin(TH_SKY, 0, 300);
    ground(0, 9);
    tree(12, 7, 5);
    enemy(14, 6, 'h');
    enemy(20, 6, 'e');
    tree(27, 5, 3);
    put(28, 1, "M");
    tree(33, 8, 5);
    enemy(35, 7, 'b');
    enemy(42, 6, 'E');
    tree(48, 6, 4);
    enemy(49, 3, 'p');
    enemy(55, 7, 'e');
    tree(62, 8, 3);
    tree(67, 5, 5);
    enemy(69, 4, 'h');
    coins(67, 2, 5);
    enemy(75, 6, 'E');
    tree(81, 7, 3);
    enemy(87, 6, 'e');
    tree(94, 8, 5);
    enemy(96, 7, 's');
    tree(102, 6, 3);
    put(103, 2, "S");
    enemy(108, 7, 'E');
    tree(114, 5, 4);
    enemy(115, 2, 'p');
    enemy(121, 6, 'e');
    tree(128, 8, 3);
    tree(133, 6, 5);
    enemy(135, 5, 'h');
    tree(141, 8, 3);
    ground(147, 175);
    goal(157);
    st.checkpoint = 62;
}

static void s5_4(void) {
    begin(TH_CASTLE, 0, 300);
    floor_w(0, 12);
    roof(0, 160, 1);
    lava_pit(13, 16, 1);
    floor_w(17, 30);
    firebar(20, 7, 0);
    firebar(26, 7, 0);
    lava_pit(31, 34, 1);
    fill(35, 8, 37, 11, 'W');
    lava_pit(38, 41, 1);
    fill(42, 6, 44, 11, 'W');
    firebar(43, 2, 1);
    lava_pit(45, 48, 1);
    floor_w(49, 74);
    fill(49, 2, 74, 5, 'W');
    firebar(54, 9, 0);
    firebar(60, 9, 0);
    firebar(66, 9, 0);
    lava_pit(75, 78, 1);
    floor_w(79, 109);
    put(83, 6, "M");
    enemy(88, 9, 'h');
    firebar(94, 6, 1);
    cannon(100, 2);
    boss_bridge(110, 122, 7);
    st.checkpoint = 49;
}

// ---- ワールド 6: 夜。敵が多い ----

static void s6_1(void) {
    begin(TH_OVER, 0, 400);
    st.night = 1;
    ground(0, 38);
    put(8, 6, "M");
    walkers(12, 's', 3, 2);
    cannon(20, 3);
    walkers(24, 'h', 1, 0);
    plant_pipe(30, 4);
    ground(43, 80);
    walkers(45, 'p', 3, 3);
    put(50, 6, "B?B?B?B");
    put(52, 2, "BSB");
    cannon(60, 2);
    walkers(63, 'b', 2, 2);
    plateau(70, 74, 7);
    plateau(75, 80, 5);
    ground(85, 130);
    walkers(88, 'h', 1, 0);
    walkers(92, 's', 3, 2);
    plant_pipe(100, 3);
    cannon(106, 2);
    put(112, 6, "?M?");
    walkers(114, 'p', 2, 3);
    ground(135, 200);
    put(138, 6, "l");
    walkers(142, 'b', 4, 2);
    cannon(152, 3);
    walkers(156, 'h', 1, 0);
    stairs(164, 8, 1);
    tower(172, 8);
    goal(181);
    st.checkpoint = 94;
}

static void s6_2(void) {
    begin(TH_UNDER, 0, 400);
    ground(0, 24);
    ceiling(4, 158);
    put(8, 6, "M");
    walkers(12, 's', 3, 2);
    plant_pipe(20, 3);
    ground(29, 60);
    fill(30, 6, 31, 9, 'B');
    walkers(34, 'h', 1, 0);
    fill(38, 6, 39, 9, 'B');
    put(44, 3, "BBBBBB");
    put(44, 6, "B$BBSB");
    walkers(46, 'b', 3, 2);
    plant_pipe(54, 4);
    ground(65, 105);
    walkers(66, 'p', 3, 3);
    plateau(76, 80, 7);
    enemy(78, 6, 's');
    plant_pipe(86, 3);
    plant_pipe(92, 4);
    walkers(97, 'b', 2, 3);
    ground(110, 170);
    fill(112, 7, 120, 7, 'B');
    coins(112, 9, 9);
    walkers(114, 'g', 3, 2);
    put(125, 6, "?M?");
    walkers(130, 'h', 1, 0);
    walkers(136, 's', 3, 2);
    put(145, 6, "l");
    plant_pipe(149, 2);
    stairs(154, 4, 1);
    goal(166);
    st.checkpoint = 82;
}

static void s6_3(void) {
    begin(TH_SKY, 0, 300);
    st.night = 1;
    ground(0, 8);
    enemy(11, 7, 'e');
    tree(18, 5, 3);
    put(19, 1, "M");
    enemy(24, 5, 'E');
    tree(30, 8, 3);
    enemy(30, 5, 'p');
    enemy(36, 6, 'e');
    tree(43, 6, 3);
    enemy(44, 5, 's');
    enemy(49, 5, 'E');
    tree(55, 8, 5);
    enemy(57, 7, 'h');
    enemy(63, 6, 'e');
    tree(70, 5, 3);
    coins(70, 2, 3);
    enemy(75, 6, 'E');
    tree(81, 8, 3);
    enemy(81, 4, 'p');
    enemy(87, 7, 'e');
    tree(94, 6, 3);
    put(95, 2, "S");
    enemy(100, 6, 'E');
    tree(106, 8, 4);
    enemy(107, 7, 's');
    enemy(113, 6, 'e');
    tree(120, 5, 3);
    enemy(120, 2, 'p');
    enemy(126, 6, 'E');
    tree(132, 8, 3);
    enemy(138, 7, 'e');
    tree(145, 7, 3);
    ground(151, 180);
    goal(160);
    st.checkpoint = 56;
}

static void s6_4(void) {
    begin(TH_CASTLE, 0, 300);
    floor_w(0, 12);
    roof(0, 170, 1);
    lava_pit(13, 17, 1);
    fill(15, 8, 15, 11, 'W');
    floor_w(18, 34);
    firebar(22, 6, 1);
    firebar(30, 8, 0);
    lava_pit(35, 38, 1);
    fill(39, 7, 41, 11, 'W');
    firebar(40, 3, 0);
    lava_pit(42, 45, 1);
    fill(46, 5, 48, 11, 'W');
    lava_pit(49, 53, 1);
    fill(51, 8, 51, 11, 'W');
    floor_w(54, 80);
    fill(54, 2, 80, 5, 'W');
    firebar(58, 9, 0);
    firebar(64, 9, 0);
    firebar(70, 9, 0);
    firebar(76, 9, 0);
    lava_pit(81, 85, 1);
    fill(83, 8, 83, 11, 'W');
    floor_w(86, 117);
    put(90, 6, "M");
    enemy(95, 9, 'h');
    cannon(100, 3);
    firebar(106, 6, 1);
    boss_bridge(118, 130, 8);
    st.checkpoint = 54;
}

// ---- ワールド 7: 何でも出る ----

static void s7_1(void) {
    begin(TH_OVER, 0, 400);
    ground(0, 36);
    put(8, 6, "?M?");
    cannon(14, 2);
    cannon(18, 3);
    walkers(22, 'b', 3, 2);
    plant_pipe(30, 4);
    ground(41, 76);
    walkers(42, 'h', 1, 0);
    put(48, 6, "BBBBBB");
    put(48, 2, "BB$SBB");
    walkers(50, 's', 3, 2);
    cannon(58, 4);
    walkers(62, 'p', 3, 3);
    plant_pipe(70, 3);
    plateau(79, 79, 8);
    ground(82, 124);
    walkers(84, 'h', 1, 0);
    cannon(90, 2);
    walkers(94, 'b', 4, 2);
    plateau(104, 108, 7);
    enemy(106, 6, 's');
    plant_pipe(114, 4);
    walkers(118, 'p', 2, 3);
    plateau(127, 127, 8);
    ground(130, 200);
    put(134, 6, "l");
    walkers(138, 'h', 1, 0);
    cannon(144, 3);
    walkers(148, 's', 4, 2);
    put(158, 6, "B?BMB");
    stairs(166, 8, 1);
    tower(174, 8);
    goal(183);
    st.checkpoint = 100;
}

static void s7_2(void) {
    begin(TH_UNDER, 0, 400);
    ground(0, 22);
    ceiling(4, 158);
    put(8, 6, "M");
    walkers(12, 's', 3, 2);
    plant_pipe(19, 4);
    ground(27, 58);
    fill(28, 5, 36, 5, 'B');
    walkers(31, 'h', 1, 0);
    walkers(38, 'b', 3, 2);
    plant_pipe(46, 3);
    plant_pipe(52, 4);
    ground(63, 104);
    plateau(64, 67, 8);
    plateau(68, 71, 6);
    put(68, 2, "BSBB");
    enemy(70, 5, 'h');
    walkers(76, 'p', 3, 3);
    cannon(88, 2);
    walkers(92, 's', 3, 2);
    ground(109, 170);
    fill(110, 7, 120, 7, 'B');
    coins(110, 9, 11);
    walkers(112, 'b', 3, 3);
    plant_pipe(126, 3);
    put(132, 6, "?M?");
    walkers(136, 'h', 1, 0);
    walkers(142, 's', 3, 2);
    put(150, 6, "l");
    stairs(154, 4, 1);
    goal(166);
    st.checkpoint = 76;
}

static void s7_3(void) {
    begin(TH_SKY, 0, 300);
    ground(0, 8);
    enemy(11, 7, 'e');
    tree(18, 6, 3);
    enemy(19, 3, 'p');
    enemy(24, 6, 'E');
    tree(30, 5, 3);
    put(31, 1, "M");
    enemy(36, 6, 'e');
    tree(43, 8, 5);
    enemy(45, 7, 'h');
    enemy(50, 5, 'E');
    tree(56, 6, 3);
    enemy(56, 3, 'p');
    enemy(62, 7, 'e');
    tree(69, 5, 3);
    enemy(70, 4, 's');
    enemy(75, 6, 'E');
    tree(81, 8, 3);
    enemy(87, 6, 'e');
    tree(94, 6, 3);
    put(95, 2, "S");
    enemy(100, 5, 'E');
    tree(106, 8, 3);
    enemy(106, 5, 'p');
    enemy(112, 7, 'e');
    tree(119, 5, 5);
    enemy(121, 4, 'h');
    enemy(126, 6, 'E');
    tree(132, 8, 3);
    enemy(138, 6, 'e');
    tree(145, 7, 3);
    ground(151, 180);
    goal(160);
    st.checkpoint = 56;
}

static void s7_4(void) {
    begin(TH_CASTLE, 0, 300);
    floor_w(0, 10);
    roof(0, 180, 1);
    lava_pit(11, 15, 1);
    fill(13, 8, 13, 11, 'W');
    floor_w(16, 30);
    firebar(19, 7, 1);
    firebar(26, 7, 0);
    lava_pit(31, 35, 1);
    fill(33, 7, 33, 11, 'W');
    fill(36, 6, 40, 11, 'W');
    firebar(38, 2, 0);
    lava_pit(41, 45, 1);
    fill(43, 8, 43, 11, 'W');
    floor_w(46, 76);
    fill(46, 2, 76, 5, 'W');
    firebar(50, 9, 0);
    firebar(56, 9, 0);
    firebar(62, 9, 0);
    firebar(68, 9, 0);
    enemy(72, 9, 's');
    lava_pit(77, 82, 1);
    fill(79, 8, 80, 11, 'W');
    floor_w(83, 121);
    put(87, 6, "M");
    cannon(92, 2);
    enemy(96, 9, 'h');
    firebar(102, 6, 1);
    firebar(110, 6, 1);
    boss_bridge(122, 134, 9);
    st.checkpoint = 46;
}

// ---- ワールド 8: 最後 ----

static void s8_1(void) {
    begin(TH_OVER, 0, 400);
    ground(0, 30);
    put(8, 6, "M");
    walkers(12, 's', 3, 2);
    cannon(20, 3);
    plateau(33, 33, 8);
    ground(36, 64);
    walkers(37, 'h', 1, 0);
    plant_pipe(42, 4);
    walkers(47, 'p', 3, 3);
    put(54, 6, "B?B?B");
    cannon(60, 4);
    plateau(67, 67, 7);
    ground(70, 100);
    walkers(71, 'b', 4, 2);
    plateau(80, 84, 6);
    enemy(82, 5, 'h');
    plant_pipe(90, 3);
    walkers(94, 's', 2, 3);
    plateau(103, 103, 8);
    ground(106, 140);
    put(108, 6, "?S?");
    walkers(112, 'p', 4, 3);
    cannon(124, 2);
    cannon(128, 3);
    cannon(132, 4);
    plateau(143, 143, 8);
    ground(146, 205);
    put(150, 6, "l");
    put(154, 6, "M");
    walkers(156, 'h', 1, 0);
    walkers(160, 's', 4, 2);
    stairs(172, 8, 1);
    tower(180, 8);
    goal(190);
    st.checkpoint = 106;
}

static void s8_2(void) {
    begin(TH_UNDER, 0, 400);
    ground(0, 20);
    ceiling(4, 166);
    put(8, 6, "M");
    walkers(12, 's', 3, 2);
    ground(25, 50);
    plant_pipe(29, 4);
    walkers(34, 'h', 1, 0);
    walkers(38, 'b', 3, 2);
    fill(44, 6, 45, 9, 'B');
    ground(55, 90);
    plateau(56, 60, 7);
    enemy(58, 6, 'h');
    put(64, 6, "BBBBBBB");
    put(64, 3, "BB$MBBB");
    walkers(66, 's', 3, 2);
    plant_pipe(78, 3);
    plant_pipe(84, 4);
    ground(95, 135);
    walkers(96, 'p', 3, 3);
    cannon(106, 3);
    walkers(110, 'b', 4, 2);
    plant_pipe(122, 3);
    walkers(127, 'h', 1, 0);
    ground(140, 185);
    put(142, 6, "l");
    walkers(146, 's', 4, 2);
    put(156, 6, "?S?");
    stairs(162, 4, 1);
    goal(174);
    st.checkpoint = 96;
}

static void s8_3(void) {
    begin(TH_SKY, 0, 300);
    ground(0, 8);
    enemy(11, 7, 'e');
    tree(18, 5, 3);
    enemy(18, 2, 'p');
    enemy(24, 6, 'E');
    tree(30, 7, 5);
    enemy(32, 6, 'h');
    enemy(38, 6, 'e');
    tree(45, 5, 3);
    put(46, 1, "M");
    enemy(51, 6, 'E');
    tree(57, 8, 3);
    enemy(57, 5, 'p');
    enemy(63, 7, 'e');
    tree(70, 6, 5);
    enemy(72, 5, 'h');
    enemy(78, 5, 'E');
    tree(84, 8, 3);
    enemy(85, 7, 's');
    enemy(90, 6, 'e');
    tree(97, 5, 3);
    put(98, 1, "S");
    enemy(103, 6, 'E');
    tree(109, 8, 3);
    enemy(109, 5, 'p');
    enemy(115, 7, 'e');
    tree(122, 6, 5);
    enemy(124, 5, 'h');
    enemy(130, 5, 'E');
    tree(136, 8, 3);
    enemy(142, 6, 'e');
    tree(149, 7, 3);
    ground(155, 185);
    goal(165);
    st.checkpoint = 72;
}

static void s8_4(void) {
    begin(TH_CASTLE, 0, 400);
    floor_w(0, 10);
    roof(0, 230, 1);
    lava_pit(11, 15, 1);
    fill(13, 8, 13, 11, 'W');
    floor_w(16, 34);
    firebar(20, 6, 1);
    firebar(28, 6, 1);
    lava_pit(35, 40, 1);
    fill(37, 7, 38, 11, 'W');
    fill(41, 6, 44, 11, 'W');
    firebar(42, 2, 0);
    lava_pit(45, 50, 1);
    fill(47, 8, 48, 11, 'W');
    floor_w(51, 84);
    fill(51, 2, 84, 5, 'W');
    firebar(55, 9, 0);
    firebar(61, 9, 0);
    firebar(67, 9, 0);
    firebar(73, 9, 0);
    firebar(79, 9, 0);
    lava_pit(85, 90, 1);
    fill(87, 7, 88, 11, 'W');
    floor_w(91, 120);
    put(95, 6, "M");
    enemy(100, 9, 'h');
    cannon(106, 2);
    enemy(110, 9, 's');
    enemy(113, 9, 's');
    firebar(116, 6, 1);
    lava_pit(121, 126, 1);
    fill(123, 8, 124, 11, 'W');
    floor_w(127, 160);
    fill(127, 2, 160, 4, 'W');
    firebar(132, 8, 0);
    firebar(140, 8, 1);
    firebar(148, 8, 0);
    enemy(154, 9, 'h');
    lava_pit(161, 166, 1);
    fill(163, 7, 164, 11, 'W');
    floor_w(167, 191);
    put(171, 6, "S");
    firebar(178, 6, 1);
    boss_bridge(192, 206, 10);
    st.checkpoint = 91;
}

static void (*const builders[STAGES])(void) = {
    s1_1, s1_2, s1_3, s1_4, s2_1, s2_2, s2_3, s2_4, s3_1, s3_2, s3_3, s3_4, s4_1, s4_2, s4_3, s4_4,
    s5_1, s5_2, s5_3, s5_4, s6_1, s6_2, s6_3, s6_4, s7_1, s7_2, s7_3, s7_4, s8_1, s8_2, s8_3, s8_4,
};

void build_stage(int n) {
    switch (n) {
    case -1: stage_test(); break;
    default: builders[n](); break;
    }
    decorate(n);
    // 出てくる場所を列の順に並べる
    for (int i = 1; i < nspawns; i++)
        for (int j = i; j > 0 && spawns[j - 1].col > spawns[j].col; j--) {
            struct spawn s = spawns[j];
            spawns[j] = spawns[j - 1];
            spawns[j - 1] = s;
        }
}
