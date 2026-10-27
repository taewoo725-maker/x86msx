/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * scc.c - 코나미 SCC / SCC-I 음원 (2.11 다시 짬)
 *
 * - 레지스터를 쓰기 직전까지의 소리를 먼저 만들어 둔다(scc_render) → 프레임 안에서 바뀌는 파형·음량·주파수가
 *   정확한 시점에 반영된다 (전에는 프레임 끝 값으로 한 프레임 전체를 만들었음).
 * - 출력 샘플마다 그 구간(1/48000초)에 지나간 파형 계단의 넓이 평균(상자 필터)을 낸다 → 높은 음에서
 *   계단을 한 점씩 집어 생기던 거친 잡음(에일리어싱)이 줄어든다.
 * - 칩 출력처럼 파형(8비트) x 음량(4비트) / 16을 5채널 더하고, 아주 약한 저역 통과(약 13 kHz)로 마무리.
 */
#include "scc.h"
#include <string.h>

void scc_reset(SCC *s) { memset(s, 0, sizeof *s); }

void scc_set_stereo(SCC *s)
{
    static const int8_t pn[5] = { 0, -67, 67, -67, 67 };
    for (int c = 0; c < 5; c++) s->pan[c] = pn[c];
    s->st = 1;
}

static uint32_t scc_inc(const SCC *s, int c)
{
    uint32_t p = s->period[c];
    if (s->deform & 2) p &= 0xFF;                               /* 변형 레지스터 비트 1: 주기 하위 8비트만, 비트 0: 위 4비트만 (openMSX SCC.cc 와 같게 — 5.0.50, 전에는 1/16·1/256 로 잘못 나눔) */
    else if (s->deform & 1) p >>= 8;
    if (p <= 8) return 0;                                       /* 주기 8 이하: 파형이 안 넘어가고 지금 값에 멈춤 (음량 레지스터로 소리를 내는 샘플 재생에 쓰임) */
    return (uint32_t)(((uint64_t)3579545u << 16) / ((uint64_t)(p + 1) * 48000u));   /* 샘플당 파형 계단 수 (16.16) */
}

/* 이번 프레임 버퍼를 upto 샘플까지 채움 */
void scc_render(SCC *s, int upto)
{
    if (upto > SCC_BUF_MAX) upto = SCC_BUF_MAX;
    if (s->pos >= upto) return;
    uint32_t inc[5];
    for (int c = 0; c < 5; c++) inc[c] = (s->enable & (1 << c)) ? scc_inc(s, c) : 0;
    int wl[5], wr[5];
    for (int c = 0; c < 5; c++) { wl[c] = 100 - (s->pan[c] > 0 ? s->pan[c] : 0); wr[c] = 100 + (s->pan[c] < 0 ? s->pan[c] : 0); }
    for (int i = s->pos; i < upto; i++) {
        int32_t sum = 0, sl = 0, sr = 0;
        for (int c = 0; c < 5; c++) {
            uint32_t n = inc[c];
            if (!n) {
                if (s->enable & (1 << c)) {                     /* 멈춘 채널도 지금 파형 값 × 음량이 나감 (openMSX 와 같게) */
                    int32_t x = (int32_t)s->wave[c][(s->phase[c] >> 16) & 31] * s->vol[c];
                    sum += x;
                    if (s->st) { sl += x * wl[c] / 100; sr += x * wr[c] / 100; }
                }
                continue;
            }
            const int8_t *w = s->wave[c];
            uint32_t p = s->phase[c], rem = n;
            int32_t area = 0;                                   /* 최대 128 x n (약 6천만), 32비트로 충분 */
            while (rem) {                                       /* 이 샘플 구간에 걸친 계단들의 넓이 */
                uint32_t left = 0x10000u - (p & 0xFFFFu), d = rem < left ? rem : left;
                area += (int32_t)w[(p >> 16) & 31] * (int32_t)d;
                p += d; rem -= d;
            }
            s->phase[c] = p & 0x1FFFFFu;                        /* 32계단 x 65536 */
            int32_t x = (area / (int32_t)n) * s->vol[c];        /* -128..127 x 0..15 */
            sum += x;
            if (s->st) { sl += x * wl[c] / 100; sr += x * wr[c] / 100; }
        }
        if (s->st) {
            s->lpl += ((sl << 4) - s->lpl) * 13 >> 4; s->lpr += ((sr << 4) - s->lpr) * 13 >> 4;
            s->bufl[i] = (int16_t)(s->lpl >> 4); s->bufr[i] = (int16_t)(s->lpr >> 4);
        }
        s->lp += ((sum << 4) - s->lp) * 13 >> 4;                /* 약한 저역 통과 (계수 0.81, 약 13 kHz) */
        s->buf[i] = (int16_t)(s->lp >> 4);
    }
    s->pos = upto;
}

void scc_mix(SCC *s, int16_t *out, int n, int vol)
{
    scc_render(s, n);
    if (n > SCC_BUF_MAX) n = SCC_BUF_MAX;
    if (vol > 0 && (s->enable || s->lp))
        for (int i = 0; i < n; i++) {
            int v = out[i] + s->buf[i] * 2 * vol / 100;         /* 5채널 최대 약 ±19000 */
            out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    s->pos = 0;
}

void scc_mix_st(SCC *s, int16_t *out, int16_t *lout, int16_t *rout, int n, int vol)
{
    scc_render(s, n);
    if (n > SCC_BUF_MAX) n = SCC_BUF_MAX;
    int on = vol > 0 && (s->enable || s->lp || s->lpl || s->lpr);
    for (int i = 0; i < n; i++) {
        int l = on ? s->bufl[i] * 2 * vol / 100 : 0, r = on ? s->bufr[i] * 2 * vol / 100 : 0;
        lout[i] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
        rout[i] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
    }
    scc_mix(s, out, n, vol);
}

/* 변형 레지스터 비트 6·7: 파형 쓰기 금지 (비트 7 은 진짜 SCC 만) — openMSX SCC.cc setDeformRegHelper */
static int scc_ro(const SCC *s, int ch, int real)
{
    int m = s->deform & (real ? 0xC0 : 0x40);
    return m == 0x40 || m == 0xC0 || (m == 0x80 && ch >= 3);
}

void scc_write(SCC *s, uint8_t off, uint8_t v)
{
    if (off < 0x80) {                                   /* 파형: 채널 1~4 */
        if (scc_ro(s, off >> 5, 1)) return;
        s->wave[off >> 5][off & 31] = (int8_t)v;
        if ((off >> 5) == 3) s->wave[4][off & 31] = (int8_t)v;
        return;
    }
    if (off < 0xA0) {                                   /* 0x80~0x8F (0x90~0x9F 거울) */
        uint8_t r = off & 0x0F;
        if (r < 10) {
            int ch = r >> 1;
            if (r & 1) s->period[ch] = (uint16_t)((s->period[ch] & 0x0FF) | ((v & 0x0F) << 8));
            else s->period[ch] = (uint16_t)((s->period[ch] & 0xF00) | v);
            if (s->deform & 0x20) s->phase[ch] = 0;     /* 변형 비트 5: 주파수를 쓰면 파형 처음부터 */
        } else if (r < 15) s->vol[r - 10] = v & 0x0F;
        else s->enable = v & 0x1F;
        return;
    }
    if (off >= 0xE0) s->deform = v;
}

uint8_t scc_read(SCC *s, uint8_t off)
{
    if (off < 0x80) return (uint8_t)s->wave[off >> 5][off & 31];
    if (off >= 0xA0 && off < 0xC0) return (uint8_t)s->wave[4][off & 31];
    return 0xFF;
}

void scc_write_plus(SCC *s, uint8_t off, uint8_t v)
{
    if (off < 0xA0) { if (scc_ro(s, off >> 5, 0)) return; s->wave[off >> 5][off & 31] = (int8_t)v; return; }     /* 채널 5도 자기 파형 */
    if (off < 0xC0) { scc_write(s, (uint8_t)(0x80 | (off & 0x0F)), v); return; }
    if (off < 0xE0) s->deform = v;
}

uint8_t scc_read_plus(SCC *s, uint8_t off)
{
    if (off < 0xA0) return (uint8_t)s->wave[off >> 5][off & 31];
    return 0xFF;
}
