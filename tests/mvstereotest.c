/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mvstereotest.c - 8.0.232: X86 Multimedia 소리의 좌우. mvsnd 의 '밖에서 넣는 PCM' 길로 스테레오/모노를 넣고
 *   1) 모노 줄은 예전과 같다: 모노 파일이면 왼쪽 == 오른쪽 == 모노
 *   2) 스테레오(왼쪽만 소리)면 오른쪽은 조용하고 (왼쪽+오른쪽)/2 == 모노
 *   3) 재생 위치(소리 시계)는 모노든 스테레오든 같은 속도로 감 */
#include "../src/mvsnd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int total, fails;
#define CHECK(c, ...) do { total++; if (!(c)) { fails++; printf("FAIL: "); } else printf("PASS: "); printf(__VA_ARGS__); printf("\n"); } while (0)

#define N 4800
static int16_t mono[N], left[N], right[N], in2[N * 2], in1[N];

static void play_block(int ch, const int16_t *x, int hz)
{
    mvsnd_reset(); mvsnd_ext_open(); mvsnd_ext_clear();
    mvsnd_ext_push(x, N, ch, hz); mvsnd_ext_finish(); mvsnd_ext_play(1);
    mvsnd_render_stereo(mono, left, right, 2400);
}

int main(void)
{
    for (int i = 0; i < N; i++) { in1[i] = (int16_t)(8000 * sin(i * 0.1)); in2[i * 2] = in1[i]; in2[i * 2 + 1] = 0; }
    /* 모노 파일 */
    play_block(1, in1, 48000);
    int same = 1, pk = 0; for (int i = 0; i < 2400; i++) { if (left[i] != mono[i] || right[i] != mono[i]) same = 0; if (abs(mono[i]) > pk) pk = abs(mono[i]); }
    CHECK(same && pk > 4000, "모노 파일: 왼쪽 == 오른쪽 == 모노 (최대 %d)", pk);
    uint32_t e_mono; mvsnd_state_get(0, &e_mono, 0);
    /* 스테레오 파일, 왼쪽만 소리 */
    play_block(2, in2, 48000);
    int bad = 0, pl = 0, pr = 0; for (int i = 0; i < 2400; i++) { if (abs((left[i] + right[i]) / 2 - mono[i]) > 1) bad++; if (abs(left[i]) > pl) pl = abs(left[i]); if (abs(right[i]) > pr) pr = abs(right[i]); }
    CHECK(bad == 0 && pl > 7000 && pr < 4, "스테레오(왼쪽만): 좌 %d 우 %d, (L+R)/2 == 모노 어긋남 %d", pl, pr, bad);
    uint32_t e_st; mvsnd_state_get(0, &e_st, 0);
    CHECK(e_mono == e_st && e_st == 2400, "소리 시계: 모노 %u, 스테레오 %u (둘 다 2400)", e_mono, e_st);
    /* 스테레오 파일, 양쪽 같은 소리 → 모노 줄은 평균 = 같은 소리 */
    for (int i = 0; i < N; i++) { in2[i * 2] = in1[i]; in2[i * 2 + 1] = in1[i]; }
    play_block(2, in2, 48000);
    int dif = 0; for (int i = 0; i < 2400; i++) if (abs(left[i] - mono[i]) > 1 || abs(right[i] - mono[i]) > 1) dif++;
    CHECK(dif == 0, "스테레오(양쪽 같은 소리): 왼쪽 == 오른쪽 == 모노, 어긋남 %d", dif);
    /* 일반 렌더(모노만)는 그대로 */
    int16_t only[2400]; play_block(2, in2, 48000);
    mvsnd_reset(); mvsnd_ext_open(); mvsnd_ext_clear(); mvsnd_ext_push(in2, N, 2, 48000); mvsnd_ext_finish(); mvsnd_ext_play(1); mvsnd_render(only, 2400);
    CHECK(memcmp(only, mono, sizeof only) == 0, "mvsnd_render(모노만) 결과가 좌우 판의 모노 줄과 같음");
    printf("mvstereotest: %d/%d passed\n", total - fails, total);
    return fails != 0;
}
