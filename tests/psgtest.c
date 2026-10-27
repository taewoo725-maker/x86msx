/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * psgtest.c - PSG 단위 시험: 톤 주파수, 엔벌로프 모양, 잡음, 믹서, 키 클릭
 */
#include "../src/psg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PSG p;
static int fails, total;
static int16_t out[48000 * 4];
static int nout;

/* seconds 동안 생성 (프레임 단위로 버퍼를 비워 가며) */
static uint64_t now;

static void run(double seconds)
{
    uint64_t end = now + (uint64_t)(seconds * 3579545.0);
    nout = 0;
    while (now < end) {
        now += 59736;
        if (now > end) now = end;
        p.buf_n = 0;
        psg_sync(&p, now);
        for (int i = 0; i < p.buf_n && nout < (int)(sizeof out / 2); i++) out[nout++] = p.buf[i];
    }
}

static double freq(void)
{
    double mean = 0;
    for (int i = 0; i < nout; i++) mean += out[i];
    mean /= nout;
    int c = 0;
    for (int i = 1; i < nout; i++) if (out[i - 1] < mean && out[i] >= mean) c++;
    return c * 48000.0 / nout;
}

static void check(const char *name, int ok, const char *detail)
{
    total++;
    if (!ok) fails++;
    printf("%s %-36s %s\n", ok ? "PASS" : "FAIL", name, detail);
}

static void tone_test(int period)
{
    char d[64];
    psg_reset(&p, now);
    psg_write(&p, 0, (uint8_t)period); psg_write(&p, 1, (uint8_t)(period >> 8));
    psg_write(&p, 7, 0x3E); psg_write(&p, 8, 15);
    run(0.1); run(1.0);
    double want = 1789772.5 / (16.0 * period), got = freq();
    snprintf(d, sizeof d, "%.1f Hz (want %.1f)", got, want);
    char n[40]; snprintf(n, sizeof n, "tone A period %d", period);
    check(n, got > want * 0.995 && got < want * 1.005, d);
}

/* 엔벌로프: 톤/잡음을 모두 끄면 출력 = 엔벌로프 음량 그대로 */
static void env_setup(int shape, int ep)
{
    psg_reset(&p, now);
    psg_write(&p, 7, 0x3F);             /* 톤·잡음 모두 끔 → 채널은 항상 켜짐 */
    psg_write(&p, 8, 0x10);             /* A: 엔벌로프 */
    psg_write(&p, 11, (uint8_t)ep); psg_write(&p, 12, (uint8_t)(ep >> 8));
    psg_write(&p, 13, (uint8_t)shape);
}

int main(void)
{
    char d[96];
    tone_test(254);     /* 440 Hz */
    tone_test(100);
    tone_test(1000);
    tone_test(4000);

    /* 엔벌로프 한 단계 = 2*EP 틱 = 32*EP T, 16단계 */
    int ep = 100;                       /* 한 주기 = 16*32*100 T = 14.3 ms */
    double cycle_s = 16.0 * 32 * ep / 3579545.0;

    env_setup(0x00, ep); run(cycle_s * 0.1);
    int start = out[nout - 1];
    run(cycle_s * 3);
    snprintf(d, sizeof d, "start %d, after 3 cycles %d", start, out[nout - 1]);
    check("envelope 0x00 \\___ decays and holds 0", start > 4000 && out[nout - 1] == 0, d);

    env_setup(0x0D, ep); run(cycle_s * 0.1);
    start = out[nout - 1];
    run(cycle_s * 3);
    snprintf(d, sizeof d, "start %d, after 3 cycles %d", start, out[nout - 1]);
    check("envelope 0x0D /~~~ rises and holds max", start < 1000 && out[nout - 1] == 7000, d);

    env_setup(0x08, ep); run(1.0);
    snprintf(d, sizeof d, "%.1f Hz (want %.1f)", freq(), 1.0 / cycle_s);
    check("envelope 0x08 \\\\\\\\ sawtooth repeats", freq() > 0.98 / cycle_s && freq() < 1.02 / cycle_s, d);

    env_setup(0x0E, ep); run(1.0);
    snprintf(d, sizeof d, "%.1f Hz (want %.1f)", freq(), 0.5 / cycle_s);
    check("envelope 0x0E /\\/\\ triangle, half rate", freq() > 0.49 / cycle_s && freq() < 0.51 / cycle_s, d);

    /* 잡음: 톤 끄고 잡음만 → 0과 최대 사이를 불규칙하게 오감 */
    psg_reset(&p, now);
    psg_write(&p, 6, 5); psg_write(&p, 7, 0x37); psg_write(&p, 8, 15);
    run(0.5);
    int hi = 0, lo = 0;
    for (int i = 0; i < nout; i++) { if (out[i] > 3500) hi++; else lo++; }
    snprintf(d, sizeof d, "high %d%%, crossing rate %.0f Hz", hi * 100 / nout, freq());
    check("noise on channel A", hi > nout / 5 && lo > nout / 5 && freq() > 1000, d);

    /* 믹서: 모든 채널 끔(음량 0) → 무음 */
    psg_reset(&p, now);
    run(0.1);
    int maxv = 0;
    for (int i = 0; i < nout; i++) if (out[i] > maxv) maxv = out[i];
    snprintf(d, sizeof d, "max %d", maxv);
    check("silence after reset", maxv == 0, d);

    /* 키 클릭 */
    psg_set_click(&p, 1);
    run(0.01);
    snprintf(d, sizeof d, "level %d", out[nout - 1]);
    check("key click level", out[nout - 1] > 0, d);

    printf("psgtest: %d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
