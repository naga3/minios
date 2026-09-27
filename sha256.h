// SHA-256（FIPS 180-4）。シェルが /data/bin のプログラムを実行する前に、記録されたハッシュと照らし合わせるのに使う
#pragma once
#include "lib.h"

struct sha256 {
    unsigned int h[8];
    unsigned char buf[64];
    unsigned long len;  // これまでに入れたバイト数
};

static const unsigned int sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static unsigned int ror(unsigned int x, int n) { return (x >> n) | (x << (32 - n)); }

static void sha256_block(struct sha256 *s, const unsigned char *p) {
    unsigned int w[64];
    for (int i = 0; i < 16; i++) w[i] = p[4 * i] << 24 | p[4 * i + 1] << 16 | p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; i++) {
        unsigned int s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
        unsigned int s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    unsigned int a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
    for (int i = 0; i < 64; i++) {
        unsigned int t1 = h + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + sha256_k[i] + w[i];
        unsigned int t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
    s->h[0] += a, s->h[1] += b, s->h[2] += c, s->h[3] += d, s->h[4] += e, s->h[5] += f, s->h[6] += g, s->h[7] += h;
}

static void sha256_init(struct sha256 *s) {
    static const unsigned int h0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for (int i = 0; i < 8; i++) s->h[i] = h0[i];
    s->len = 0;
}

static void sha256_update(struct sha256 *s, const unsigned char *p, size_t n) {
    while (n--) {
        s->buf[s->len++ % 64] = *p++;
        if (s->len % 64 == 0) sha256_block(s, s->buf);
    }
}

// 64 文字の 16 進数と NUL を hex に書く
static void sha256_final(struct sha256 *s, char hex[65]) {
    unsigned long bits = s->len * 8;
    unsigned char pad = 0x80;
    sha256_update(s, &pad, 1);
    pad = 0;
    while (s->len % 64 != 56) sha256_update(s, &pad, 1);
    for (int i = 7; i >= 0; i--) {
        unsigned char b = bits >> (8 * i);
        sha256_update(s, &b, 1);
    }
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        unsigned char b = s->h[i / 4] >> (24 - 8 * (i % 4));
        hex[2 * i] = digits[b >> 4];
        hex[2 * i + 1] = digits[b & 15];
    }
    hex[64] = 0;
}

// ファイルの SHA-256。0 か負のエラー番号
static long sha256_file(const char *path, char hex[65]) {
    long fd = syscall3(SYS_open, path, O_RDONLY, 0);
    if (fd < 0) return fd;
    struct sha256 s;
    sha256_init(&s);
    unsigned char buf[4096];
    long n;
    while ((n = syscall3(SYS_read, fd, buf, sizeof buf)) > 0) sha256_update(&s, buf, n);
    syscall3(SYS_close, fd, 0, 0);
    if (n < 0) return n;
    sha256_final(&s, hex);
    return 0;
}
