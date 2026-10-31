/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* 5.2.148 소리 속도 맞춤 시험: 440 Hz 사인을 프레임 흔들림(지연이 들쭉날쭉)과 0.13% 빠른 생산에서 넣어도 음높이가 몇 센트 안에서 일정한지 */
#define _GNU_SOURCE
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../kernel/sndrs.h"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
int main(void)
{
    const int TARGET = 1600;
    SndRs r; sndrs_reset(&r, TARGET);
    static int16_t in[2048], ol[4096], orr[4096], out[48000 * 121];
    int outn = 0; double ph = 0; int d = TARGET;
    unsigned seed = 1;
    for (int fr = 0; fr < 60 * 120; fr++) {                     /* 5.3.15: 2분 (꾸준한 차이를 배우는 데 1분쯤 걸림) */
        int n = 801;                                         /* 60.00 Hz 화면에 맞춘 쪽 생산 = 48060 /s (0.125% 많음) */
        for (int i = 0; i < n; i++) { in[i] = (int16_t)(8000 * sin(ph)); ph += 2 * M_PI * 440.0 / 48000; }
        seed = seed * 1103515245u + 12345u;
        int jit = (int)((seed >> 16) % 300) - 150;           /* 프레임 시간 흔들림 +-150 샘플 */
        int32_t step = sndrs_step2(&r, d + jit, TARGET);
        int o = sndrs_run(&r, in, 0, n, step, ol, orr, 4096);
        memcpy(&out[outn], ol, o * 2); outn += o;
        d += o - 800;                                        /* 소리 장치는 프레임마다 800 샘플(48000/60)을 먹음 */
    }
    /* 구간별 주파수: 영점 통과 개수로 */
    double minf = 1e9, maxf = 0;
    for (int s = 48000; s + 48000 * 4 <= outn; s += 24000) {  /* 4초 창(0.25 Hz = 1센트 눈금), 0.5초씩 */
        int z = 0; for (int i = s + 1; i < s + 48000 * 4; i++) if (out[i - 1] < 0 && out[i] >= 0) z++;
        double f = z / 4.0; if (f < minf) minf = f; if (f > maxf) maxf = f;
    }
    double cents = 1200 * log2(maxf / minf);
    printf("440 Hz 구간 %.2f ~ %.2f Hz, 차이 %.1f 센트, 마지막 지연 %d\n", minf, maxf, cents, d);
    CHECK(cents < 3.0, "음높이 흔들림 %.1f 센트 (3 이하여야 함)", cents);   /* 5.3.15: 8 → 3 */
    CHECK(d > TARGET - 900 && d < TARGET + 900, "지연이 목표 근처에 머묾 (%d)", d);
    /* 5.3.15: 프레임이 한 번 크게 늦어 지연이 1000 샘플 모자라게 된 뒤 — 음높이는 2센트 안에서만 움직이고 지연은 천천히 돌아와야 */
    {
        SndRs r2; sndrs_reset(&r2, TARGET); int d2 = TARGET; int32_t smin = 1 << 30, smax = 0; int back = -1;
        for (int fr = 0; fr < 60 * 60; fr++) {
            if (fr == 600) d2 -= 1000;
            int32_t st = sndrs_step2(&r2, d2, TARGET);
            if (fr > 600) { if (st < smin) smin = st; if (st > smax) smax = st; }
            d2 += 800 - (int)(800LL * st / 65536);          /* 800 만들고 st 속도로 먹음 */
            if (fr > 600 && back < 0 && d2 > TARGET - SNDRS_DEAD) back = fr - 600;
        }
        double c2 = 1200 * log2((double)smax / (double)smin);
        printf("끊김 뒤 속도 %d ~ %d (%.1f 센트), 띠 안으로 %d 프레임 뒤, 마지막 지연 %d\n", smin, smax, c2, back, d2);
        CHECK(c2 < 5.0, "끊김 뒤 음높이 변화 %.1f 센트 (전에는 9)", c2);
        CHECK(back > 0 && back < 60 * 40, "지연이 돌아옴 (%d 프레임)", back);
    }
    /* 연속성: 사인파라서 한 샘플 변화가 크지 않아야 (딱 소리 없음) */
    int maxjump = 0; for (int i = 1; i < outn; i++) { int j = out[i] - out[i - 1]; if (j < 0) j = -j; if (j > maxjump) maxjump = j; }
    CHECK(maxjump < 700, "샘플 사이 튐 %d", maxjump);
    /* 같은 속도에서는 값이 그대로 */
    SndRs q; sndrs_reset(&q, TARGET); for (int i = 0; i < 801; i++) in[i] = (int16_t)(i * 5);
    int o = sndrs_run(&q, in, 0, 801, 65536, ol, orr, 4096);
    CHECK(o == 801 && ol[400] == 1995 && ol[800] == 3995, "속도 1일 때 그대로 (o=%d)", o);
    /* 5.3.16: 처음 켠 뒤 생산이 0.13%·0.25% 많을 때 링 버퍼(4096)가 넘치거나(3696) 비지(400) 않아야 (넘치면 프레임을 버려 버벅임) */
    for (int sp = 0; sp < 3; sp++) {
        int prod[3] = { 801, 802, 799 };                  /* +0.13%, +0.25%, -0.13% */
        SndRs r3; sndrs_reset(&r3, TARGET); int d3 = TARGET, bad = 0, dmin = d3, dmax = d3;
        for (int fr = 0; fr < 60 * 120; fr++) {
            int32_t st = sndrs_step2(&r3, d3, TARGET);
            d3 += (int)(prod[sp] * 65536LL / st) - 800;
            if (d3 > 3696 || d3 < 400) bad++;
            if (d3 < dmin) dmin = d3; if (d3 > dmax) dmax = d3;
        }
        printf("생산 %d: 지연 %d ~ %d, 넘침·빔 %d 프레임, 마지막 %d\n", prod[sp], dmin, dmax, bad, d3);
        CHECK(bad == 0, "생산 %d 에서 버퍼 넘침·빔 %d 프레임", prod[sp], bad);
    }
    /* 기본 루틴(5.3.14, 그대로): 원래 기준 — 20초, 8센트 안, 지연 +-500 */
    {
        SndRs r0; sndrs_reset(&r0, TARGET); int d0 = TARGET; unsigned sd = 7; int32_t mn = 1 << 30, mx = 0;
        for (int fr = 0; fr < 60 * 20; fr++) {
            sd = sd * 1103515245u + 12345u; int jit = (int)((sd >> 16) % 300) - 150;
            int32_t st = sndrs_step(&r0, d0 + jit, TARGET);
            if (fr > 300) { if (st < mn) mn = st; if (st > mx) mx = st; }
            d0 += (int)(801 * 65536LL / st) - 800;
        }
        double c0 = 1200 * log2((double)mx / (double)mn);
        printf("기본 루틴: 속도 %d ~ %d (%.1f 센트), 마지막 지연 %d\n", mn, mx, c0, d0);
        CHECK(c0 < 8.0, "기본 루틴 음높이 흔들림 %.1f 센트", c0);
        CHECK(d0 > TARGET - 500 && d0 < TARGET + 500, "기본 루틴 지연 (%d)", d0);
    }
    printf(fails ? "sndrstest: %d 실패\n" : "sndrstest: OK\n", fails);
    return fails != 0;
}
