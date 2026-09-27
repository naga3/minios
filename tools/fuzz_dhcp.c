// DHCP の応答の解析（net.h の dhcp_find）をホストでファジングする。
// ネットワークから来る信用できないデータを PID 1 の中で解析しているので、範囲外アクセスがあると OS ごと落ちる。
//   gcc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -o build/fuzz_dhcp tools/fuzz_dhcp.c && build/fuzz_dhcp
#include "../net.h"

static unsigned long rng = 88172645463325252UL;
static unsigned long next(void) {
    rng ^= rng << 13, rng ^= rng >> 7, rng ^= rng << 17;
    return rng;
}

int main(void) {
    long rounds = 2000000, found = 0;
    for (long i = 0; i < rounds; i++) {
        // 受け取った長さ n の分だけをヒープに確保する。n を 1 バイトでも越えて読めば ASan が止める
        long n = 240 + next() % (sizeof(struct dhcp_msg) - 240 + 1);
        unsigned char *buf = __builtin_malloc(n);
        for (long k = 0; k < n; k++) buf[k] = next();
        // オプション部分はそれらしい形にもする（コード・長さ・中身の並び、長さが嘘のもの、終端なし、など）
        if (next() % 2) {
            long k = 240;
            while (k < n) {
                buf[k++] = next() % 4 == 0 ? 0 : next() % 60;   // パディング(0)か、よくあるコード
                if (k < n) buf[k++] = next() % 8 == 0 ? 255 : next() % 16;  // 長さ（時々とても長い）
                k += next() % 16;
            }
        }
        unsigned char out[4] = {0};
        int max = 1 + next() % 4;
        int code = next() % 4 == 0 ? next() % 256 : (int[]){1, 3, 6, 53, 54}[next() % 5];
        int len = dhcp_find((const struct dhcp_msg *)buf, n, code, out, max);
        if (len >= 0) found++;
        __builtin_free(buf);
    }
    print("rounds "); print_u(rounds); print(", options found "); print_u(found); print(", no errors\n");
    return 0;
}
