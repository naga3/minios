// 物理メモリ（4KB のページ）の割り当てと、プロセスごとのページテーブル
#include "kernel.h"

extern u64 pml4[512];  // boot.S が作ったカーネルのページテーブル

static u64 next_free, free_end, free_start_;
static u64 free_list;  // 返されたページの連結リスト（先頭の 8 バイトに次のページ）

void kmemset(void *d, int c, size_t n) {
    u8 *p = d;
    while (n--) *p++ = c;
}

void kmemcpy(void *d, const void *s, size_t n) {
    u8 *p = d;
    const u8 *q = s;
    while (n--) *p++ = *q++;
}

void mm_init(u64 start, u64 end) {
    next_free = free_start_ = (start + PAGE - 1) & ~(PAGE - 1);
    free_end = end & ~(PAGE - 1);
}

u64 page_alloc(void) {
    u64 p;
    if (free_list) {
        p = free_list;
        free_list = *(u64 *)P2V(p);
    } else if (next_free < free_end) {
        p = next_free;
        next_free += PAGE;
    } else {
        return 0;
    }
    kmemset(P2V(p), 0, PAGE);
    return p;
}

u64 mm_total_pages(void) { return (free_end - free_start_) / PAGE; }

u64 mm_free_pages(void) {
    u64 n = (free_end - next_free) / PAGE;
    for (u64 p = free_list; p; p = *(u64 *)P2V(p)) n++;
    return n;
}

void page_free(u64 pa) {
    *(u64 *)P2V(pa) = free_list;
    free_list = pa;
}

u64 pages_alloc_contig(u64 n) {
    if (next_free + n * PAGE > free_end) return 0;
    u64 p = next_free;
    next_free += n * PAGE;
    kmemset(P2V(p), 0, n * PAGE);
    return p;
}

// プロセスのページテーブル: 高い番地（物理メモリ全体、PML4[256]）はカーネルと共有する。
// 低い番地は、最初の 2MB（カーネルのコードとデータ）だけをカーネル専用で写し、残りはプロセスが使う
u64 *pml4_new(void) {
    u64 *p = P2V(page_alloc());
    u64 *pdpt = P2V(page_alloc());
    u64 *pd = P2V(page_alloc());
    p[256] = pml4[256];
    p[0] = V2P(pdpt) | PTE_P | PTE_W | PTE_U;
    pdpt[0] = V2P(pd) | PTE_P | PTE_W | PTE_U;
    pd[0] = 0x0 | 0x83;  // 0〜2MB をそのままの番地で（2MB のページ、カーネル専用）
    return p;
}

// va（4KB 境界）を pa に写す。途中の表は無ければ作る。0 か -1（メモリ不足）
int map_page(u64 *pml4v, u64 va, u64 pa, u64 flags) {
    u64 *t = pml4v;
    for (int level = 3; level > 0; level--) {
        u64 i = (va >> (12 + 9 * level)) & 511;
        if (!(t[i] & PTE_P)) {
            u64 n = page_alloc();
            if (!n) return -1;
            t[i] = n | PTE_P | PTE_W | PTE_U;
        }
        t = P2V(t[i] & 0x000ffffffffff000UL);
    }
    t[(va >> 12) & 511] = pa | flags | PTE_P;
    return 0;
}

void *user_ptr(u64 *pml4v, u64 va) {
    u64 *t = pml4v;
    for (int level = 3; level >= 0; level--) {
        u64 e = t[(va >> (12 + 9 * level)) & 511];
        if (!(e & PTE_P)) return 0;
        if (level == 0) return (u8 *)P2V(e & 0x000ffffffffff000UL) + (va & (PAGE - 1));
        t = P2V(e & 0x000ffffffffff000UL);
    }
    return 0;
}

#define ADDR(e) ((e) & 0x000ffffffffff000UL)

// ユーザーの部分（PML4 の 0〜255）の 4KB のページをたどり、fn(va, pa) を呼ぶ。2MB のページ（カーネルの最初の 2MB）は飛ばす
static int walk_user(u64 *pml4v, int (*fn)(u64 va, u64 pa, void *ctx), void *ctx) {
    for (u64 i = 0; i < 256; i++) {
        if (!(pml4v[i] & PTE_P)) continue;
        u64 *pdpt = P2V(ADDR(pml4v[i]));
        for (u64 j = 0; j < 512; j++) {
            if (!(pdpt[j] & PTE_P)) continue;
            u64 *pd = P2V(ADDR(pdpt[j]));
            for (u64 k = 0; k < 512; k++) {
                if (!(pd[k] & PTE_P) || (pd[k] & 0x80)) continue;
                u64 *pt = P2V(ADDR(pd[k]));
                for (u64 l = 0; l < 512; l++) {
                    if (!(pt[l] & PTE_P)) continue;
                    u64 va = i << 39 | j << 30 | k << 21 | l << 12;
                    if (fn(va, ADDR(pt[l]), ctx) < 0) return -1;
                }
            }
        }
    }
    return 0;
}

static int copy_one(u64 va, u64 pa, void *ctx) {
    u64 n = page_alloc();
    if (!n) return -1;
    kmemcpy(P2V(n), P2V(pa), PAGE);
    return map_page(ctx, va, n, PTE_W | PTE_U);
}

int copy_user_space(u64 *from, u64 *to) { return walk_user(from, copy_one, to); }

static int free_one(u64 va, u64 pa, void *ctx) {
    (void)va, (void)ctx;
    page_free(pa);
    return 0;
}

// ユーザーのページを返し、途中の表も返す（表の一番上と、pml4_new が作った最初の表は残す）
void free_user_space(u64 *pml4v) {
    walk_user(pml4v, free_one, 0);
    for (u64 i = 0; i < 256; i++) {
        if (!(pml4v[i] & PTE_P)) continue;
        u64 *pdpt = P2V(ADDR(pml4v[i]));
        for (u64 j = 0; j < 512; j++) {
            if (!(pdpt[j] & PTE_P)) continue;
            u64 *pd = P2V(ADDR(pdpt[j]));
            for (u64 k = 0; k < 512; k++) {
                if (!(pd[k] & PTE_P) || (pd[k] & 0x80)) continue;
                page_free(ADDR(pd[k]));
                pd[k] = 0;
            }
        }
    }
}

void pml4_free(u64 *pml4v) {
    free_user_space(pml4v);
    for (u64 i = 0; i < 256; i++) {
        if (!(pml4v[i] & PTE_P)) continue;
        u64 *pdpt = P2V(ADDR(pml4v[i]));
        for (u64 j = 0; j < 512; j++)
            if (pdpt[j] & PTE_P) page_free(ADDR(pdpt[j]));
        page_free(ADDR(pml4v[i]));
    }
    page_free(V2P(pml4v));
}
