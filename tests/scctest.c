/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* scctest.c - SCC 주파수·음량·켜기 시험 */
#include "../src/scc.h"
#include <stdio.h>
#include <string.h>
static int16_t buf[48000];
/* 2.11: SCC는 프레임(최대 1024샘플)마다 섞으므로 800샘플씩 */
static void mix_all(SCC *s, int16_t *b, int n, int vol) { for (int i = 0; i < n; i += 800) scc_mix(s, b + i, n - i < 800 ? n - i : 800, vol); }
static int crossings(void) { int c = 0; for (int i = 1; i < 48000; i++) if (buf[i - 1] < 0 && buf[i] >= 0) c++; return c; }
int main(void)
{
    SCC s; scc_reset(&s); int fails = 0;
    for (int i = 0; i < 32; i++) scc_write(&s, (uint8_t)i, i < 16 ? 100 : (uint8_t)-100);   /* 채널 1 사각파 */
    scc_write(&s, 0x80, 253 & 0xFF); scc_write(&s, 0x81, 253 >> 8);                      /* 주기 253 → 440 Hz */
    scc_write(&s, 0x8A, 15); scc_write(&s, 0x8F, 0x01);
    memset(buf, 0, sizeof buf); mix_all(&s, buf, 48000, 100);
    int c = crossings(); printf("%s 440 Hz tone: %d crossings\n", c >= 435 && c <= 445 ? "PASS" : "FAIL", c); fails += !(c >= 435 && c <= 445);
    scc_write(&s, 0x8F, 0x00); memset(buf, 0, sizeof buf); mix_all(&s, buf, 48000, 100);
    int silent = 1; for (int i = 0; i < 48000; i++) if (buf[i]) silent = 0;
    printf("%s channel disable gives silence\n", silent ? "PASS" : "FAIL"); fails += !silent;
    printf("%s waveform read back\n", scc_read(&s, 5) == 100 ? "PASS" : "FAIL"); fails += scc_read(&s, 5) != 100;
    /* 5채널 모두 최대 음량·최대 파형이어도 16비트 안 (잘리지 않음) */
    SCC t; scc_reset(&t);
    for (int c = 0; c < 4; c++) for (int i = 0; i < 32; i++) scc_write(&t, (uint8_t)(c * 32 + i), i < 16 ? 127 : (uint8_t)-128);
    for (int c = 0; c < 5; c++) { scc_write(&t, (uint8_t)(0x80 + c * 2), 253); scc_write(&t, (uint8_t)(0x8A + c), 15); }
    scc_write(&t, 0x8F, 0x1F);
    memset(buf, 0, sizeof buf); scc_mix(&t, buf, 48000, 100);
    int peak = 0; for (int i = 0; i < 48000; i++) { int a = buf[i] < 0 ? -buf[i] : buf[i]; if (a > peak) peak = a; }
    printf("%s 5 channels full: peak %d (no clipping)\n", peak < 32000 ? "PASS" : "FAIL", peak); fails += peak >= 32000;
    printf("scctest: %s\n", fails ? "FAILED" : "all passed");
    return fails;
}
