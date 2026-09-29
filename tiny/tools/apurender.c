// ファミコンの音源（apu.c）を PC の上で動かし、波形を出す（48,000 Hz・16 ビット・モノラルの生データを標準出力へ）。
// 引数は順に実行する: 4000=bf（レジスタに書く）、@500（500 ミリ秒ぶん作る）、smooth（音量の変化をなめらかにする）
//   cc -DHOST -I tiny -o build/apurender tiny/tools/apurender.c tiny/apu.c
//   build/apurender 4015=01 4000=bf 4001=08 4002=fd 4003=08 @500 > a.raw
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void apu_write(int addr, int v);
void apu_reset(void);
void apu_render(short *buf, int n);
void apu_smooth(int on);

int main(int argc, char **argv) {
    apu_reset();
    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (!strcmp(a, "smooth")) {
            apu_smooth(1);
        } else if (a[0] == '@') {
            int n = atoi(a + 1) * 48000 / 1000;
            short *buf = calloc(n, sizeof *buf);
            apu_render(buf, n);
            fwrite(buf, sizeof *buf, n, stdout);
            free(buf);
        } else {
            int addr, v;
            if (sscanf(a, "%x=%x", &addr, &v) != 2) return fprintf(stderr, "bad arg %s\n", a), 1;
            apu_write(addr, v);
        }
    }
    return 0;
}
