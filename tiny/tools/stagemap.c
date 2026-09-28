// ROBO JUMP のステージを PC の上で作って、文字の地図として出す（OS の中と同じ stages.c を使う）。
// 敵などは地図の上に重ねて出す。簡単な確かめ（幅、スタートとチェックポイントの地面、跳べない穴）もする
//   cc -I tiny -o build/stagemap tiny/tools/stagemap.c tiny/stages.c && build/stagemap [ステージの番号 0〜31]
#include <stdio.h>
#include <stdlib.h>
#include "stage.h"

static int ground_col(int c) {      // この列に乗れる所があるか
    for (int r = 1; r < ROWS; r++)
        if (tile_solid(map[r][c]) && !tile_solid(map[r - 1][c])) return 1;
    return 0;
}

// 立てるマス（小さいロボット 1 マス分）: そのマスが空いていて、下が通れない。リフトの動く範囲も立てるとみなす
static char stand[ROWS][MAX_COLS];

static int passable(int c, int r) { return r < 0 || (c >= 0 && c < st.width && !tile_solid(map[r][c])); }

static void mark_stands(void) {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < st.width; c++) stand[r][c] = r + 1 < ROWS && passable(c, r) && tile_solid(map[r + 1][c]);
    for (int i = 0; i < nspawns; i++) {
        struct spawn *s = &spawns[i];
        if (s->type == 'e')
            for (int c = s->col - 2; c <= s->col + 4; c++)
                if (c >= 0 && c < st.width && s->row >= 1) stand[s->row - 1][c] = 1;
        if (s->type == 'E')
            for (int r = s->row - 4; r <= s->row + 2; r++)
                for (int c = s->col; c <= s->col + 2; c++)
                    if (r >= 0 && r < ROWS && c < st.width) stand[r][c] = 1;
    }
}

// 跳んで届く横の距離（マス）。上がる高さ dh ごと（4 マスまで）
static int reach(int dh) { return dh >= 4 ? 3 : dh == 3 ? 5 : dh == 2 ? 6 : dh == 1 ? 7 : 8; }

// スタートからゴール（棒か斧）まで、跳んで行けるか。行けた一番右の列を返す
static int reachable(void) {
    static char seen[ROWS][MAX_COLS];
    static int queue[ROWS * MAX_COLS];
    int head = 0, tail = 0, best = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < MAX_COLS; c++) seen[r][c] = 0;
    for (int r = ROWS - 1; r >= 0; r--)
        if (stand[r][2]) {
            seen[r][2] = 1;
            queue[tail++] = r * MAX_COLS + 2;
            break;
        }
    while (head < tail) {
        int r = queue[head] / MAX_COLS, c = queue[head] % MAX_COLS;
        head++;
        if (c > best) best = c;
        if (!passable(c, r - 1)) {              // 頭の上が詰まっていたら、横に歩くだけ
            for (int d = -1; d <= 1; d += 2)
                if (c + d >= 0 && c + d < st.width && stand[r][c + d] && !seen[r][c + d]) {
                    seen[r][c + d] = 1;
                    queue[tail++] = r * MAX_COLS + c + d;
                }
            continue;
        }
        for (int r2 = r - 4; r2 < ROWS; r2++)
            for (int c2 = c - 8; c2 <= c + 8; c2++) {
                if (r2 < 0 || c2 < 0 || c2 >= st.width || !stand[r2][c2] || seen[r2][c2]) continue;
                int dh = r - r2, dx = c2 > c ? c2 - c : c - c2;
                if (dx > reach(dh)) continue;
                int top = (r < r2 ? r : r2) - 1, ok = 1;   // 間の列: 高い方の 1 つ上を通れること
                for (int cc = (c < c2 ? c : c2) + 1; cc < (c < c2 ? c2 : c); cc++)
                    if (!passable(cc, top)) ok = 0;
                if (!ok) continue;
                seen[r2][c2] = 1;
                queue[tail++] = r2 * MAX_COLS + c2;
            }
    }
    return best;
}

static int show(int n) {
    build_stage(n);
    static const char *themes[] = { "over", "under", "sky", "castle" };
    printf("== %d-%d  theme=%s%s width=%d time=%d checkpoint=%d goal=%d axe=%d boss_hp=%d spawns=%d\n",
           n / 4 + 1, n % 4 + 1, themes[st.theme], st.night ? " night" : "", st.width, st.time, st.checkpoint,
           st.goal, st.axe, st.boss_hp, nspawns);
    char view[ROWS][MAX_COLS + 1];
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < st.width; c++) view[r][c] = map[r][c];
        view[r][st.width] = 0;
    }
    for (int i = 0; i < nspawns; i++)
        if (spawns[i].row < ROWS && spawns[i].col < st.width) view[spawns[i].row][spawns[i].col] = spawns[i].type;
    for (int x = 0; x < st.width; x += 100) {
        printf("   ");
        for (int c = x; c < x + 100 && c < st.width; c++) putchar(c % 10 == 0 ? '0' + (c / 10) % 10 : ' ');
        putchar('\n');
        for (int r = 0; r < ROWS; r++) printf("%2d %.*s\n", r, 100, view[r] + x);
    }
    int bad = 0, run = 0, worst = 0;
    for (int c = 0; c < st.width; c++) {
        int lift = 0;
        for (int i = 0; i < nspawns; i++)
            if ((spawns[i].type == 'e' || spawns[i].type == 'E') && abs(spawns[i].col + 1 - c) <= 4) lift = 1;
        run = ground_col(c) || lift ? 0 : run + 1;
        if (run > worst) worst = run;
    }
    if (worst > 4) printf("!! gap of %d columns\n", worst), bad = 1;
    if (!ground_col(2)) printf("!! no ground at start\n"), bad = 1;
    if (st.checkpoint && !ground_col(st.checkpoint)) printf("!! no ground at checkpoint\n"), bad = 1;
    if (!st.goal && !st.axe) printf("!! no goal\n"), bad = 1;
    if (st.width > MAX_COLS) printf("!! too wide\n"), bad = 1;
    mark_stands();
    int far = reachable(), need = st.goal ? st.goal - 1 : st.axe;
    if (far < need) printf("!! cannot reach the goal: stuck at column %d (need %d)\n", far, need), bad = 1;
    return bad;
}

int main(int argc, char **argv) {
    int bad = 0;
    if (argc > 1) return show(atoi(argv[1]));
    for (int n = 0; n < STAGES; n++) bad |= show(n);
    return bad;
}
