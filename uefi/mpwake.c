/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mpwake.c - 8.0.6 다중CPU: 펌웨어 MP Services 없이 APIC 로 다른 코어 하나를 깨워 보는 시험 (Windows·Linux 와 같은 표준 순서, 코드는 직접 작성).
 *
 * 순서 (Intel SDM 의 MP 초기화):
 *  1. 1 MiB 아래에 4쪽을 잡는다: [0]=시작 코드(mpwake_tramp.asm)  [1]=PML4  [2]=PDPT  [3]=PD (0~1 GiB 항등 사상, CR3 를 32비트로 읽는 코어를 위해 4 GiB 아래)
 *  2. 깨울 코어에 INIT → 10 ms → SIPI(쪽 번호) → 200 µs → SIPI
 *  3. 깨어난 코어는 16→32→64비트로 올라가 자기 APIC ID 와 표시(0xA5A5A5A5)를 적고 HLT. 우리는 표시를 최대 200 ms 기다림
 *  4. 끝나면 그 코어에 INIT 을 다시 보내 "SIPI 기다리는 상태"로 되돌리고(재움), 응답이 있었으면 잡은 쪽을 돌려줌 (응답이 없으면 코어가 어디서 도는지 모르므로 쪽을 그대로 둠)
 * 깨어난 코어는 이 4쪽 밖의 메모리를 읽거나 쓰지 않고 펌웨어 함수도 부르지 않는다.
 */
#include "mpwake.h"
#include "mpwake_tramp.inc"

#define MW_PAGES 4
static inline void wrmsr_(uint32_t m, uint64_t v) { __asm__ volatile("wrmsr" : : "c"(m), "a"((uint32_t)v), "d"((uint32_t)(v >> 32))); }

/* ICR 에 쓰기. 반환 0 = 성공, 1 = 전달 중 표시가 안 꺼짐 */
static int ipi(int x2, uint64_t lapic, uint32_t dest, uint32_t lo)
{
    if (x2) { wrmsr_(0x830, ((uint64_t)dest << 32) | lo); return 0; }
    volatile uint32_t *icr_lo = (volatile uint32_t *)(uintptr_t)(lapic + 0x300), *icr_hi = (volatile uint32_t *)(uintptr_t)(lapic + 0x310);
    for (int k = 0; k < 1000000 && (*icr_lo & 0x1000); k++) __asm__ volatile("pause");
    *icr_hi = dest << 24;
    *icr_lo = lo;
    for (int k = 0; k < 1000000; k++) { if (!(*icr_lo & 0x1000)) return 0; __asm__ volatile("pause"); }
    return 1;
}
#define ICR_INIT 0x00004500u         /* INIT, 레벨 올림(assert) */
#define ICR_SIPI 0x00004600u         /* 시작 IPI, 하위 8비트 = 시작 쪽 번호 */

void mpwake_run(EFI_BOOT_SERVICES *bs, uint32_t target, int x2apic, uint64_t lapic_base, MpWakeResult *r)
{
    r->status = MW_NONE; r->target = target; r->woke_id = 0xFFFFFFFFu; r->us = 0; r->base = 0; r->x2apic = x2apic;
    if (!bs || target == 0xFFFFFFFFu || (!x2apic && target > 254)) { r->status = MW_NOTARGET; return; }
    EFI_PHYSICAL_ADDRESS a = 0x9FFFF;                                  /* 마지막 바이트가 이 주소 이하 = 640 KiB 아래 */
    if (bs->AllocatePages(AllocateMaxAddress, 4 /* EfiBootServicesData */, MW_PAGES, &a) != EFI_SUCCESS) { r->status = MW_NOMEM; return; }
    if ((a & 0xFFF) || a + MW_PAGES * 4096ull > 0xA0000ull) { bs->FreePages(a, MW_PAGES); r->status = MW_BADMEM; return; }
    r->base = (uint32_t)a;
    volatile uint8_t *m = (volatile uint8_t *)(uintptr_t)a;
    for (uint32_t i = 0; i < MW_PAGES * 4096u; i++) m[i] = 0;
    for (uint32_t i = 0; i < sizeof mpwake_tramp; i++) m[i] = mpwake_tramp[i];
    volatile uint64_t *pml4 = (volatile uint64_t *)(m + 0x1000), *pdpt = (volatile uint64_t *)(m + 0x2000), *pd = (volatile uint64_t *)(m + 0x3000);
    pml4[0] = (a + 0x2000) | 3; pdpt[0] = (a + 0x3000) | 3;
    for (uint64_t i = 0; i < 512; i++) pd[i] = (i << 21) | 0x83;       /* 존재 + 쓰기 + 2 MiB 쪽 */
    volatile uint32_t *flag = (volatile uint32_t *)(m + 0x840), *aid = (volatile uint32_t *)(m + 0x844);
    __asm__ volatile("mfence" ::: "memory");

    uint32_t vec = (uint32_t)(a >> 12) & 0xFF;
    int bad = 0;
    bad |= ipi(x2apic, lapic_base, target, ICR_INIT);
    bs->Stall(10000);                                                   /* 10 ms */
    bad |= ipi(x2apic, lapic_base, target, ICR_SIPI | vec);
    bs->Stall(200);
    bad |= ipi(x2apic, lapic_base, target, ICR_SIPI | vec);
    uint32_t polls = 0;
    while (*flag != 0xA5A5A5A5u && polls < 2000) { bs->Stall(100); polls++; }   /* 최대 약 200 ms */
    r->us = 10000 + 200 + polls * 100;
    int ok = (*flag == 0xA5A5A5A5u);
    if (ok) r->woke_id = *aid;
    ipi(x2apic, lapic_base, target, ICR_INIT);                          /* 다시 재움: INIT 을 받은 코어는 SIPI 를 기다리는 상태가 됨 */
    bs->Stall(10000);
    if (ok) { bs->FreePages(a, MW_PAGES); r->status = bad ? MW_IPIFAIL : MW_OK; }
    else r->status = bad ? MW_IPIFAIL : MW_TIMEOUT;                    /* 응답 없음: 쪽은 돌려주지 않음 (어디서 도는지 모르므로) */
}

/* ---- 8.0.9 일꾼 모드 ---- */
int mpworkers_open(EFI_BOOT_SERVICES *bs, MpWorkerSet *w, int x2apic, uint64_t lapic_base, uint64_t cr3, uint64_t pat)
{
    w->bs = bs; w->base = 0; w->open = 0; w->x2apic = x2apic; w->lapic = lapic_base; w->cr3 = cr3; w->pat = pat; w->nstarted = 0; w->leaked = 0;
    EFI_PHYSICAL_ADDRESS a = 0x9FFFF;
    if (bs->AllocatePages(AllocateMaxAddress, 4, MW_PAGES + 1, &a) != EFI_SUCCESS) return -1;          /* 시작 코드 + PML4 + PDPT + PD + IDT */
    if ((a & 0xFFF) || a + (MW_PAGES + 1) * 4096ull > 0xA0000ull) { bs->FreePages(a, MW_PAGES + 1); return -1; }
    w->base = (uint32_t)a;
    volatile uint8_t *m = (volatile uint8_t *)(uintptr_t)a;
    for (uint32_t i = 0; i < (MW_PAGES + 1) * 4096u; i++) m[i] = 0;
    for (uint32_t i = 0; i < sizeof mpwake_tramp; i++) m[i] = mpwake_tramp[i];
    volatile uint64_t *pml4 = (volatile uint64_t *)(m + 0x1000), *pdpt = (volatile uint64_t *)(m + 0x2000), *pd = (volatile uint64_t *)(m + 0x3000);
    pml4[0] = (a + 0x2000) | 3; pdpt[0] = (a + 0x3000) | 3;
    for (uint64_t i = 0; i < 512; i++) pd[i] = (i << 21) | 0x83;
    /* IDT: 256 개 모두 "멈춤" 함수(+0x700)로. 예외가 나도 컴퓨터가 다시 시작되지 않고 그 코어만 멈춤 */
    volatile uint8_t *idt = m + 0x4000; uint64_t h = a + 0x700;
    for (int i = 0; i < 256; i++) {
        volatile uint8_t *g = idt + i * 16;
        g[0] = (uint8_t)h; g[1] = (uint8_t)(h >> 8); g[2] = 0x18; g[3] = 0; g[4] = 0; g[5] = 0x8E;
        g[6] = (uint8_t)(h >> 16); g[7] = (uint8_t)(h >> 24); g[8] = (uint8_t)(h >> 32); g[9] = (uint8_t)(h >> 40); g[10] = (uint8_t)(h >> 48); g[11] = (uint8_t)(h >> 56);
    }
    volatile uint8_t *idtr = m + 0x870; uint64_t ib = a + 0x4000;
    idtr[0] = 0xFF; idtr[1] = 0x0F; for (int i = 0; i < 8; i++) idtr[2 + i] = (uint8_t)(ib >> (8 * i));
    w->open = 1;
    return 0;
}
static void put64(volatile uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
int mpworkers_start(MpWorkerSet *w, uint32_t target, MpWCtx *ctx)
{
    if (!w->open || w->nstarted >= MPW_MAXT) return -1;
    volatile uint8_t *m = (volatile uint8_t *)(uintptr_t)w->base;
    ctx->state = 0; ctx->apic = 0xFFFFFFFFu;
    put64(m + 0x850, w->cr3); put64(m + 0x858, w->pat); put64(m + 0x860, (uint64_t)(uintptr_t)ctx);
    __asm__ volatile("mfence" ::: "memory");
    uint32_t vec = (w->base >> 12) & 0xFF; int bad = 0;
    bad |= ipi(w->x2apic, w->lapic, target, ICR_INIT);
    w->bs->Stall(10000);
    bad |= ipi(w->x2apic, w->lapic, target, ICR_SIPI | vec);
    w->bs->Stall(200);
    bad |= ipi(w->x2apic, w->lapic, target, ICR_SIPI | vec);
    uint32_t polls = 0;
    while (*(volatile uint32_t *)&ctx->state == 0 && polls < 2000) { w->bs->Stall(100); polls++; }
    w->started[w->nstarted++] = target;
    if (ctx->state == 0) { w->leaked = 1; return -1; }
    (void)bad;
    return 0;
}
void mpworkers_close(MpWorkerSet *w)
{
    if (!w->open) return;
    for (int i = 0; i < w->nstarted; i++) ipi(w->x2apic, w->lapic, w->started[i], ICR_INIT);   /* 일꾼을 다시 SIPI 기다리는 상태로 */
    w->bs->Stall(10000);
    if (!w->leaked) w->bs->FreePages(w->base, MW_PAGES + 1);
    w->open = 0;
}

static int put_s(char *b, int n, int max, const char *s) { while (*s && n < max - 1) b[n++] = *s++; return n; }
static int put_u(char *b, int n, int max, uint32_t x) { char t[12]; int k = 0; if (!x) t[k++] = '0'; while (x) { t[k++] = (char)('0' + x % 10); x /= 10; } while (k && n < max - 1) b[n++] = t[--k]; return n; }
int mpwake_text(const MpWakeResult *r, char *b, int max)
{
    static const char *const nm[] = { "off", "ok", "timeout", "no_lowmem", "bad_lowmem", "no_target", "ipi_fail" };
    int n = 0;
    if (r->status == MW_NONE) return 0;
    n = put_s(b, n, max, " wake="); n = put_s(b, n, max, nm[r->status]);
    n = put_s(b, n, max, " apic="); n = put_u(b, n, max, r->target);
    n = put_s(b, n, max, r->x2apic ? " x2" : " xa");
    if (r->status == MW_OK || r->woke_id != 0xFFFFFFFFu) { n = put_s(b, n, max, " woke_id="); n = put_u(b, n, max, r->woke_id); n = put_s(b, n, max, " us="); n = put_u(b, n, max, r->us); }
    if (r->base) { n = put_s(b, n, max, " page="); n = put_u(b, n, max, r->base >> 12); }
    b[n] = 0; return n;
}
