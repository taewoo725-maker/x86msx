#define _POSIX_C_SOURCE 199309L
/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vdpktest.c - VDP 그리기 커널 C판 대 어셈블러판 동등성 시험 (7단계). 무작위 입력·어긋난 주소·앞뒤 보호 칸까지 바이트 단위로 견줌.
 * 사용: ./build/vdpktest [반복 수=20000] [bench]   — bench 를 주면 속도도 잰다 */
#include "../src/vdpk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint32_t rs = 12345;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

#define GUARD 32
static int fails;
static void bad(const char *k, int it, int off, int n) { if (fails++ < 10) fprintf(stderr, "FAIL %s it=%d off=%d n=%d\n", k, it, off, n); }

int main(int argc, char **argv)
{
    long iters = argc > 1 ? atol(argv[1]) : 20000;
    int bench = argc > 2 && !strcmp(argv[2], "bench");
    if (!vdpk_have_asm()) { printf("vdpktest: 어셈블러판 없음 (SKIP)\n"); return 0; }
    static uint8_t src[600], oa[1200 + 2 * GUARD], ob[1200 + 2 * GUARD];
    for (long it = 0; it < iters; it++) {
        int so = (int)(rnd() % 17), oo = (int)(rnd() % 17);      /* 어긋난 주소 */
        for (int i = 0; i < 600; i++) src[i] = (uint8_t)rnd();
        if (it % 7 == 0) memset(src, (int)(rnd() & 0xFF), 600);   /* 같은 바이트 */
        uint8_t map16[16], map4[4];
        for (int i = 0; i < 16; i++) map16[i] = (uint8_t)rnd();
        for (int i = 0; i < 4; i++) map4[i] = (uint8_t)rnd();
        memset(oa, 0xA5, sizeof oa); memset(ob, 0xA5, sizeof ob);
        /* put256 */
        vdpk_c.put256(oa + GUARD + oo, src + so);  vdpk_x64.put256(ob + GUARD + oo, src + so);
        if (memcmp(oa, ob, sizeof oa)) bad("put256", (int)it, oo, 256);
        memset(oa, 0xA5, sizeof oa); memset(ob, 0xA5, sizeof ob);
        /* nib16: n = 16 의 배수 (G4 128, G6 256, 그리고 16~256 아무 배수) */
        int n = 16 * (1 + (int)(rnd() % 16));
        vdpk_c.nib16(oa + GUARD + oo, src + so, n, map16);  vdpk_x64.nib16(ob + GUARD + oo, src + so, n, map16);
        if (memcmp(oa, ob, sizeof oa)) bad("nib16", (int)it, oo, n);
        memset(oa, 0xA5, sizeof oa); memset(ob, 0xA5, sizeof ob);
        /* crumb4 */
        n = 16 * (1 + (int)(rnd() % 8));
        vdpk_c.crumb4(oa + GUARD + oo, src + so, n, map4);  vdpk_x64.crumb4(ob + GUARD + oo, src + so, n, map4);
        if (memcmp(oa, ob, sizeof oa)) bad("crumb4", (int)it, oo, n);
        /* glyph8: n = 짝수 칸 (2~64), 무늬·앞색·뒤색은 서로 다른 어긋난 주소 */
        {
            static uint8_t pa[80], fa[80], ba[80];
            for (int i = 0; i < 80; i++) { pa[i] = (uint8_t)rnd(); fa[i] = (uint8_t)rnd(); ba[i] = (uint8_t)rnd(); }
            if (it % 5 == 0) { memset(pa, 0x00, 80); } else if (it % 5 == 1) { memset(pa, 0xFF, 80); }
            n = 2 * (1 + (int)(rnd() % 32));
            int po = (int)(rnd() % 9), fo = (int)(rnd() % 9), bo = (int)(rnd() % 9);
            memset(oa, 0xA5, sizeof oa); memset(ob, 0xA5, sizeof ob);
            vdpk_c.glyph8(oa + GUARD + oo, pa + po, fa + fo, ba + bo, n); vdpk_x64.glyph8(ob + GUARD + oo, pa + po, fa + fo, ba + bo, n);
            if (memcmp(oa, ob, sizeof oa)) bad("glyph8", (int)it, oo, n);
        }
        /* n = 0 은 아무것도 쓰지 않음 */
        memset(oa, 0xA5, sizeof oa); memset(ob, 0xA5, sizeof ob);
        vdpk_x64.nib16(ob + GUARD, src, 0, map16); vdpk_x64.crumb4(ob + GUARD, src, 0, map4); vdpk_x64.glyph8(ob + GUARD, src, src, src, 0);
        if (memcmp(oa, ob, sizeof oa)) bad("n0", (int)it, 0, 0);
    }
    printf("vdpktest: %ld 반복 %s\n", iters, fails ? "실패" : "OK (C판과 어셈블러판 결과 같음)");
    if (bench && !fails) {
        const struct { const char *nm; const VdpKernels *k; } ks[2] = { { "C  ", &vdpk_c }, { "ASM", &vdpk_x64 } };
        for (int q = 0; q < 2; q++) {
            const VdpKernels *k = ks[q].k;
            struct timespec t0, t1; long N = 2000000;
            uint8_t m16[16], m4[4]; for (int i = 0; i < 16; i++) m16[i] = (uint8_t)(i * 7); for (int i = 0; i < 4; i++) m4[i] = (uint8_t)(i * 5);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (long i = 0; i < N; i++) { src[0] = (uint8_t)i; k->put256(oa, src); __asm__ volatile("" ::: "memory"); }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double a = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / N;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (long i = 0; i < N; i++) { src[0] = (uint8_t)i; k->nib16(oa, src, 128, m16); __asm__ volatile("" ::: "memory"); }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double b = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / N;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (long i = 0; i < N; i++) { src[0] = (uint8_t)i; k->crumb4(oa, src, 128, m4); __asm__ volatile("" ::: "memory"); }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double c = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / N;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            for (long i = 0; i < N; i++) { src[0] = (uint8_t)i; k->glyph8(oa, src, src + 40, src + 80, 32); __asm__ volatile("" ::: "memory"); }
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double g = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / N;
            printf("  %s  put256 %.1f ns   nib16(128) %.1f ns   crumb4(128) %.1f ns   glyph8(32) %.1f ns\n", ks[q].nm, a, b, c, g);
        }
    }
    return fails ? 1 : 0;
}
