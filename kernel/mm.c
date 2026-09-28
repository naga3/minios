// 物理メモリ（4KB のページ）の割り当てと、プロセスごとのページテーブル
#include "kernel.h"

extern u64 pml4[512];  // boot.S が作ったカーネルのページテーブル

static u64 next_free, free_end;
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
    next_free = (start + PAGE - 1) & ~(PAGE - 1);
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

u64 mm_free_pages(void) { return (free_end - next_free) / PAGE; }

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
