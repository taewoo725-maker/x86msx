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
    static int16_t in[2048], ol[4096], orr[4096], out[48000 * 21];
    int outn = 0; double ph = 0; int d = TARGET;
    unsigned seed = 1;
    for (int fr = 0; fr < 60 * 20; fr++) {
        int n = 801;                                         /* 60.00 Hz 화면에 맞춘 쪽 생산 = 48060 /s (0.125% 많음) */
        for (int i = 0; i < n; i++) { in[i] = (int16_t)(8000 * sin(ph)); ph += 2 * M_PI * 440.0 / 48000; }
        seed = seed * 1103515245u + 12345u;
        int jit = (int)((seed >> 16) % 300) - 150;           /* 프레임 시간 흔들림 +-150 샘플 */
        int32_t step = sndrs_step(&r, d + jit, TARGET);
        int o = sndrs_run(&r, in, 0, n, step, ol, orr, 4096);
        memcpy(&out[outn], ol, o * 2); outn += o;
        d += o - 800;                                        /* 소리 장치는 프레임마다 800 샘플(48000/60)을 먹음 */
    }
    /* 구간별 주파수: 영점 통과 개수로 */
    double minf = 1e9, maxf = 0;
    for (int s = 48000; s + 48000 <= outn; s += 4800) {      /* 1초 창, 0.1초씩 */
        int z = 0; for (int i = s + 1; i < s + 48000; i++) if (out[i - 1] < 0 && out[i] >= 0) z++;
        double f = z; if (f < minf) minf = f; if (f > maxf) maxf = f;
    }
    double cents = 1200 * log2(maxf / minf);
    printf("440 Hz 구간 영점 %.0f ~ %.0f, 차이 %.1f 센트, 마지막 지연 %d\n", minf, maxf, cents, d);
    CHECK(cents < 8.0, "음높이 흔들림 %.1f 센트 (8 이하여야 함)", cents);
    CHECK(d > TARGET - 500 && d < TARGET + 500, "지연이 목표로 모임 (%d)", d);
    /* 연속성: 사인파라서 한 샘플 변화가 크지 않아야 (딱 소리 없음) */
    int maxjump = 0; for (int i = 1; i < outn; i++) { int j = out[i] - out[i - 1]; if (j < 0) j = -j; if (j > maxjump) maxjump = j; }
    CHECK(maxjump < 700, "샘플 사이 튐 %d", maxjump);
    /* 같은 속도에서는 값이 그대로 */
    SndRs q; sndrs_reset(&q, TARGET); for (int i = 0; i < 801; i++) in[i] = (int16_t)(i * 5);
    int o = sndrs_run(&q, in, 0, 801, 65536, ol, orr, 4096);
    CHECK(o == 801 && ol[400] == 1995 && ol[800] == 3995, "속도 1일 때 그대로 (o=%d)", o);
    printf(fails ? "sndrstest: %d 실패\n" : "sndrstest: OK\n", fails);
    return fails != 0;
}
