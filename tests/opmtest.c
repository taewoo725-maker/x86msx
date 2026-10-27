/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* opmtest.c - 3.4.1 YM2151 (3.7.0 YM64: YM2164 OPP 타이머 B 포함): A4(KC 4Ah) 440 Hz, 옥타브, MUL, 엔벌로프 끝남, 타이머 A·B 주기, 좌우 */
#include "../src/ym64.h"
#include <stdio.h>
#include <stdlib.h>
static YM64 o;
static void w(int r, int v) { opm_write(&o, 0, (uint8_t)r); opm_write(&o, 1, (uint8_t)v); }
static double freq(int n, int *peak, int side)
{
    int cross = 0, first = -1, last = -1; int32_t prev = 0; *peak = 0;
    for (int i = 0; i < n; i++) {
        int32_t l, r; opm_calc(&o, &l, &r);
        int32_t v = side ? r : l;
        if (abs(v) > *peak) *peak = abs(v);
        if (prev < 0 && v >= 0) { if (first < 0) first = i; last = i; cross++; }
        prev = v;
    }
    return cross > 1 ? (cross - 1) * (3579545.0 / 64) / (last - first) : 0;
}
int main(void)
{
    int fails = 0, total = 0, pk;
#define CHECK(c, ...) do { total++; int ok_ = (c); fails += !ok_; printf("%s ", ok_ ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); } while (0)
    opm_reset(&o);
    w(0x20, 0xC7);                                      /* RL 둘 다, FB 0, CON 7 */
    w(0x28, 0x4A); w(0x30, 0);                          /* A4 */
    for (int k = 0; k < 4; k++) { int s = k * 8; w(0x40 + s, 0x01); w(0x60 + s, k ? 127 : 0); w(0x80 + s, 31); w(0xA0 + s, 0); w(0xC0 + s, 0); w(0xE0 + s, 0x0F); }
    w(0x08, 0x08);                                      /* M1 키 온 */
    double f = freq(55930, &pk, 0);
    CHECK(f > 437 && f < 443, "A4 = %.1f Hz (440), peak %d", f, pk);
    CHECK(pk > 6000, "full level peak %d", pk);
    w(0x28, 0x5A); f = freq(55930, &pk, 1);
    CHECK(f > 875 && f < 885, "A5 = %.1f Hz (880, right channel)", f);
    w(0x28, 0x4A); w(0x40, 0x03); f = freq(55930, &pk, 0);
    CHECK(f > 1310 && f < 1330, "MUL 3 = %.1f Hz (1320)", f);
    w(0x40, 0x01); w(0x28, 0x4E); f = freq(55930, &pk, 0);     /* C5 (옥타브 4의 C = 코드 14) */
    CHECK(f > 520 && f < 526, "KC 4Eh = %.1f Hz (C5 523.3)", f);
    w(0x08, 0x00);                                      /* 키 오프: RR 15 → 곧 조용 */
    freq(5593, &pk, 0); freq(5593, &pk, 0);
    CHECK(pk == 0, "silent after key off (peak %d)", pk);
    /* 타이머 A: NA = 1000 → 64 * 24 = 1536 클록 */
    w(0x10, 1000 >> 2); w(0x11, 1000 & 3); w(0x14, 0x15);
    opm_advance(&o, 1535);
    int a1 = o.status & 1;
    opm_advance(&o, 2);
    CHECK(!a1 && (o.status & 1) && opm_irq(&o), "timer A overflows after 1536 clocks (irq %d)", opm_irq(&o));
    w(0x12, 200);
    w(0x14, 0x15 | 0x10 | 0x20 | 0x2A);
    opm_advance(&o, 1024 * 56 - 1); int b1 = (o.status >> 1) & 1; opm_advance(&o, 2);
    CHECK(!b1 && (o.status & 2), "timer B overflows after 57344 clocks");
    w(0x14, 0x30);
    CHECK(!opm_irq(&o) && !(o.status & 3), "flags reset, irq off");
    /* 3.7.0 YM64: YM2164(OPP)는 타이머 B가 절반 빠르기 (2048클록 단위) */
    o.opp = 1; opm_reset(&o);
    CHECK(o.opp == 1, "chip kind survives reset");
    w(0x12, 200); w(0x14, 0x2A);
    opm_advance(&o, 2048 * 56 - 1); b1 = (o.status >> 1) & 1; opm_advance(&o, 2);
    CHECK(!b1 && (o.status & 2), "OPP timer B overflows after 114688 clocks");
    printf("opmtest: %d/%d passed\n", total - fails, total);
    return fails;
}
