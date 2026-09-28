// ROBO JUMP のステージ。stages.c が 1 文字 1 マスの地図を作る。
// OS の関数を使わないので、PC の上でも動かして地図を文字で見られる（tools/stagemap.c）
#pragma once

enum { TILE = 16, ROWS = 12, MAX_COLS = 256, MAX_SPAWNS = 160 };
enum { TH_OVER, TH_UNDER, TH_SKY, TH_CASTLE };   // 地上、地下、空（木の上）、城

// 地図の文字
//   通れないマス: # 地面  B れんが  ? コインの箱  M パワーアップの箱  $ コインが何枚も出るれんが  S スターのれんが
//                 U 使った箱  H 固いブロック  [ ] 土管の口（左・右） { } 土管の胴  t 木の上  W 城の石  = 橋
//                 K 砲台の頭  k 砲台の台
//   下から叩いたときだけ当たる: l 隠れた 1UP の箱
//   通れるマス:   . 空  o コイン  ~ 溶岩（触るとミス）  A 斧（触ると橋が落ちる）  | ゴールの棒  O 棒の先
//                 飾り: ( ) 雲  y 草むら  ^ / n \ 丘  i 木の幹  x m d D 城（れんが、胸壁、窓、入口）
extern char map[ROWS][MAX_COLS];

// 敵などの出てくる場所（地図とは別に持つ。列の順に並ぶ）
//   g 歩く敵  b 甲羅の敵  p 羽の甲羅の敵  f 土管の花（土管の口の左のマス）  s トゲの敵  h レンチ投げ
//   K 砲台  r R ファイアバー（短い右回り・長い左回り）  v 溶岩の火の玉  Z ボス  e 横に動くリフト  E 縦に動くリフト
struct spawn { unsigned char col, row; char type; };
extern struct spawn spawns[MAX_SPAWNS];
extern int nspawns;

extern struct stage {
    int theme, night;
    int width;          // マスの数
    int time;           // 制限時間
    int checkpoint;     // 途中から始めるときの列（0 なら無し）
    int goal;           // ゴールの棒の列（城のステージは 0）
    int axe;            // 斧の列（城のステージだけ）
    int boss_hp;
} st;

void build_stage(int n);    // n = (ワールド - 1) × 4 + (ステージ - 1)、0〜31。-1 はテスト用の小さなステージ
int tile_solid(char c);     // 通れないマスか
enum { STAGES = 32 };
