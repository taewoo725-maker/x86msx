/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* opl4test.c - 3.1.0 MoonSound(OPL4) 시험: FM 440 Hz, 웨이브(램에 만든 사각파) 피치·키 끄기, 장치 번호 */
#include "../src/opl4.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t mem[OPL4_MEM_SIZE];
static Opl4 o;
static int fails, total;
#define CHECK(c, ...) do { total++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } else { printf("PASS: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void fm(int bank, int r, int v) { opl4_write_fm(&o, bank * 2, (uint8_t)r); opl4_write_fm(&o, bank * 2 + 1, (uint8_t)v); }
static void wv(int r, int v) { opl4_write_wave(&o, 0, (uint8_t)r); opl4_write_wave(&o, 1, (uint8_t)v); }

/* 1초 동안 만들어 교차 횟수(Hz)와 최대 크기 */
static double measure(int secs, int *peak)
{
    int prev = 0, cross = 0, pk = 0;
    for (int i = 0; i < 48000 * secs; i++) {
        int s = opl4_calc48(&o);
        if (prev < 0 && s >= 0) cross++;
        prev = s;
        if (abs(s) > pk) pk = abs(s);
    }
    *peak = pk;
    return (double)cross / secs;
}

int main(void)
{
    memset(mem, 0, sizeof mem);
    opl4_set_memory(mem, 2u << 20);                     /* 롬 자리는 0 (실제 롬 없이) */
    opl4_reset(&o);
    int pk;
    /* FM: 채널 0, 반송파만, 440 Hz (블록 4, F-number 580) */
    fm(1, 5, 1);
    fm(0, 0x20, 1); fm(0, 0x40, 63); fm(0, 0x60, 0xF0); fm(0, 0x80, 0x0F);
    fm(0, 0x23, 1); fm(0, 0x43, 0); fm(0, 0x63, 0xF0); fm(0, 0x83, 0x0F);
    fm(0, 0xC0, 0x31); fm(0, 0xA0, 0x44); fm(0, 0xB0, 0x32);
    double f = measure(1, &pk);
    CHECK(f > 437 && f < 443 && pk > 4000, "FM 440 Hz: %.1f Hz peak %d", f, pk);
    fm(0, 0xB0, 0x12);                                  /* 키 끔 */
    measure(1, &pk);
    f = measure(1, &pk);
    CHECK(pk < 50, "FM key off: peak %d", pk);
    /* 웨이브: NEW2를 켜야 보임 */
    opl4_reset(&o);
    wv(2, 0);
    opl4_write_wave(&o, 0, 2);
    CHECK(opl4_read_wave(&o, 1) == 0xFF, "wave hidden without NEW2");
    fm(1, 5, 3);
    opl4_write_wave(&o, 0, 2);
    CHECK((opl4_read_wave(&o, 1) & 0xE0) == 0x20, "device ID 1");
    /* 램 0x200000에 머리(음색 384) + 0x200100에 8비트 사각파 한 주기 100샘플 */
    wv(2, 1);                                           /* 실제 칩처럼 R#2 비트 0 = 메모리 쓰기 허용 */
    wv(3, 0x20); wv(4, 0x00); wv(5, 0x00);
    uint8_t h[12] = { 0x00 | 0x20, 0x01, 0x00, 0x00, 0x00, 0xFF, 0x9B, 0x00, 0xF0, 0x00, 0x0F, 0x00 };   /* 8비트, 시작 0x200100, 루프 0, 끝 100 */
    for (int i = 0; i < 12; i++) wv(6, h[i]);
    wv(3, 0x20); wv(4, 0x01); wv(5, 0x00);
    for (int i = 0; i < 100; i++) wv(6, i < 50 ? 0x60 : 0xA0);
    wv(2, 4 << 2);                                      /* 머리 자리 0x200000 (4 × 0x80000) */
    wv(0xF9, 0);
    wv(0x20, 0x01); wv(0x08, 0x80);                     /* 음색 384 (0x180): 비트 8을 먼저, 아래 8비트를 쓸 때 머리를 읽음 */
    wv(0x38, 0x00); wv(0x50, 0x00); wv(0x68, 0x80);     /* OCT 0, FN 0, TL 0, 키 켬 */
    f = measure(1, &pk);
    CHECK(f > 215 && f < 226 && pk > 4000, "wave pitch OCT 0: %.1f Hz (22050/100 = 220.5) peak %d", f, pk);
    wv(0x38, 0x10);                                     /* OCT 1 → 두 배 */
    f = measure(1, &pk);
    CHECK(f > 432 && f < 450, "wave pitch OCT 1: %.1f Hz", f);
    wv(0x68, 0x00);                                     /* 키 끔 (릴리스 15 → 빨리) */
    measure(1, &pk);
    f = measure(1, &pk);
    CHECK(pk < 50, "wave key off: peak %d", pk);
    printf("opl4test: %d/%d passed\n", total - fails, total);
    return fails != 0;
}
