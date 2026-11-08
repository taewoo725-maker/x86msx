/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* ohwptest - 8.0.126 src/oh_wp.asm 의 두 함수(oh_wp_uni, oh_wp_bi)를 OpenH264 C 식과 무작위로 맞춰 봄. 폭 2·4·8·16, logd 0~7, 음수 가중치·보정 포함. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
typedef struct { uint8_t *p; const uint8_t *q; intptr_t stride; int w, h, logd, w1, w2, off; } OhWp;
void oh_wp_uni(OhWp *a);
void oh_wp_bi(OhWp *a);
static int clip(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static uint32_t rs = 12345;
static int rnd(int lo, int hi) { rs = rs * 1664525u + 1013904223u; return lo + (int)((rs >> 8) % (uint32_t)(hi - lo + 1)); }
int main(void)
{
    static const int ws[] = { 2, 4, 8, 16 }; int bad = 0, nt = 0;
    static uint8_t a[64 * 24], b[64 * 24], c[64 * 24], g[64 * 24];
    for (int it = 0; it < 20000; it++) {
        int w = ws[rnd(0, 3)], h = rnd(1, 16), logd = rnd(0, 7), stride = 32 + rnd(0, 8) * 2;
        for (int i = 0; i < (int)sizeof a; i++) { a[i] = (uint8_t)rnd(0, 255); b[i] = (uint8_t)rnd(0, 255); }
        if (it % 7 == 0) for (int i = 0; i < (int)sizeof a; i++) { a[i] = (uint8_t)(rnd(0, 1) ? 0 : 255); b[i] = (uint8_t)(rnd(0, 1) ? 0 : 255); }   /* 끝값 */
        /* 한 방향 */
        int wt = rnd(-128, 128), off = rnd(-128, 127);
        memcpy(c, a, sizeof a); memcpy(g, a, sizeof a);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int v = a[x + y * stride];
            int t = logd >= 1 ? ((v * wt + (1 << (logd - 1))) >> logd) + off : v * wt + off;
            g[x + y * stride] = (uint8_t)clip(t);
        }
        OhWp u = { c, 0, stride, w, h, logd, wt, 0, off }; oh_wp_uni(&u);
        if (memcmp(c, g, sizeof c)) { if (bad++ < 5) printf("FAIL uni w=%d h=%d logd=%d wt=%d off=%d\n", w, h, logd, wt, off); }
        nt++;
        /* 두 방향 (암시 가중치 −64..128, 명시 −128..127 모두) */
        int w1 = rnd(-128, 128), w2 = it % 2 ? 64 - w1 : rnd(-128, 127), o1 = rnd(-128, 127), o2 = rnd(-128, 127);
        memcpy(c, a, sizeof a); memcpy(g, a, sizeof a);
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
            int t = ((a[x + y * stride] * w1 + b[x + y * stride] * w2 + (1 << logd)) >> (logd + 1)) + ((o1 + o2 + 1) >> 1);
            g[x + y * stride] = (uint8_t)clip(t);
        }
        OhWp d = { c, b, stride, w, h, logd, w1, w2, (o1 + o2 + 1) >> 1 }; oh_wp_bi(&d);
        if (memcmp(c, g, sizeof c)) { if (bad++ < 5) printf("FAIL bi w=%d h=%d logd=%d w1=%d w2=%d\n", w, h, logd, w1, w2); }
        nt++;
    }
    printf(bad ? "ohwptest: FAIL %d / %d\n" : "ohwptest: all passed (%d 경우)\n", bad ? bad : nt, nt);
    return bad != 0;
}
