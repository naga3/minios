// カーネルの小さなメモリ割り当て。32〜2048 バイトは大きさごとの空きリストから、それより大きいものは続いたページから。
// 先頭 16 バイトに、どの大きさだったかを書いておく（kfree で使う）
#include "kernel.h"

#define NBUCKET 7  // 32, 64, 128, 256, 512, 1024, 2048
static void *free_lists[NBUCKET];

void *kmalloc(u64 n) {
    n += 16;
    if (n > 2048) {
        u64 pages = (n + PAGE - 1) / PAGE;
        u64 pa = pages_alloc_contig(pages);
        if (!pa) return 0;
        u64 *h = P2V(pa);
        h[0] = 100 + pages;  // 100 以上ならページの数
        return (u8 *)h + 16;
    }
    int b = 0;
    while ((32UL << b) < n) b++;
    if (!free_lists[b]) {
        u64 pa = page_alloc();
        if (!pa) return 0;
        u64 size = 32UL << b;
        for (u64 off = 0; off + size <= PAGE; off += size) {
            void **obj = (void **)((u8 *)P2V(pa) + off);
            *obj = free_lists[b];
            free_lists[b] = obj;
        }
    }
    u64 *h = free_lists[b];
    free_lists[b] = *(void **)h;
    kmemset(h, 0, 32UL << b);
    h[0] = b;
    return (u8 *)h + 16;
}

void kfree(void *p) {
    if (!p) return;
    u64 *h = (u64 *)((u8 *)p - 16);
    if (h[0] >= 100) {
        u64 pages = h[0] - 100;  // 先に読む（1 枚目を返すと、先頭が空きリストの鎖で上書きされる）
        for (u64 i = 0; i < pages; i++) page_free(V2P(h) + i * PAGE);
        return;
    }
    u64 b = h[0];  // 先に読む（次の行で同じ場所を空きリストの鎖で上書きする）
    *(void **)h = free_lists[b];
    free_lists[b] = h;
}
