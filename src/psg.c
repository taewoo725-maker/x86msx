/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * psg.c - AY-3-8910
 *
 * 엔벌로프 상태 기계는 널리 쓰이는 방식(MAME ay8910과 같은 구조)을 따른다.
 * 출력은 내부 틱(223.7 kHz)마다 레벨을 계산해 48 kHz 샘플 구간 평균을 낸다.
 */
#include "psg.h"
#include <string.h>

#define T_PER_TICK      16                                  /* CPU T-state */
#define T_PER_SAMPLE_FP ((uint64_t)3579545 * 65536 / PSG_RATE)   /* 74.57 T, 16.16 */

/* AY-3-8910 음량: 한 단계 3 dB (openMSX AY8910.cc 의 volumeTab 과 같은 곡선, 최대 채널당 7000). 5.0.46 */
static const uint16_t vol_table[16] = {
    0, 55, 77, 109, 155, 219, 309, 438, 619, 875, 1237, 1750, 2475, 3500, 4950, 7000
};
#define CLICK_LEVEL 3000

static const uint8_t reg_mask[16] = {
    0xFF, 0x0F, 0xFF, 0x0F, 0xFF, 0x0F, 0x1F, 0xFF,
    0x1F, 0x1F, 0x1F, 0xFF, 0xFF, 0x0F, 0xFF, 0xFF
};

static void env_restart(PSG *p)
{
    uint8_t shape = p->reg[13];
    p->env_attack = (shape & 0x04) ? 0x0F : 0x00;
    if (!(shape & 0x08)) {                  /* CONT = 0: 한 번 후 0 유지 */
        p->env_hold = 1;
        p->env_alt = p->env_attack;
    } else {
        p->env_hold = shape & 0x01;
        p->env_alt = shape & 0x02;
    }
    p->env_step = 0x0F;
    p->env_holding = 0;
    p->env_cnt = 0;
}

void psg_reset(PSG *p, uint64_t now)
{
    memset(p, 0, sizeof *p);
    p->vol_tone = p->vol_click = 100;       /* 믹서 값은 프런트엔드가 매 프레임 넣음 */
    p->lfsr = 1;
    p->reg[7] = 0xFF;                       /* 모든 출력 끔 */
    env_restart(p);
    p->next_tick = now;
    p->next_sample_fp = now << 16;
}

static void tick(PSG *p)
{
    for (int c = 0; c < 3; c++) {
        uint16_t period = (uint16_t)(p->reg[c * 2] | ((p->reg[c * 2 + 1] & 0x0F) << 8));
        if (!period) period = 1;
        if (++p->tone_cnt[c] >= period) {
            p->tone_cnt[c] = 0;
            p->tone_out[c] ^= 1;
        }
    }
    uint16_t np = p->reg[6] & 0x1F;
    if (!np) np = 1;
    if (++p->noise_cnt >= np * 2) {
        p->noise_cnt = 0;
        /* 17비트 LFSR: 새 비트 = 비트0 XOR 비트3 */
        uint32_t bit = (p->lfsr ^ (p->lfsr >> 3)) & 1;
        p->lfsr = (p->lfsr >> 1) | (bit << 16);
        p->noise_out = (uint8_t)(p->lfsr & 1);
    }
    uint32_t ep = (uint32_t)(p->reg[11] | (p->reg[12] << 8));
    if (!ep) ep = 1;
    if (++p->env_cnt >= ep * 2) {
        p->env_cnt = 0;
        if (!p->env_holding) {
            p->env_step--;
            if (p->env_step < 0) {
                if (p->env_hold) {
                    if (p->env_alt) p->env_attack ^= 0x0F;
                    p->env_holding = 1;
                    p->env_step = 0;
                } else {
                    if (p->env_alt) p->env_attack ^= 0x0F;
                    p->env_step = 0x0F;
                }
            }
        }
    }
}

static int32_t level(const PSG *p)
{
    int32_t sum = 0;
    uint8_t mix = p->reg[7];
    uint8_t env = (uint8_t)(p->env_step ^ p->env_attack) & 0x0F;
    for (int c = 0; c < 3; c++) {
        int tone_on = (mix >> c) & 1 ? 1 : p->tone_out[c];
        int noise_on = (mix >> (c + 3)) & 1 ? 1 : p->noise_out;
        if (tone_on && noise_on) {
            uint8_t a = p->reg[8 + c];
            sum += vol_table[(a & 0x10) ? env : (a & 0x0F)];
        }
    }
    if (sum) ((PSG *)p)->tone_seen = 1;
    sum = sum * p->vol_tone / 100;
    if (p->click) sum += CLICK_LEVEL * p->vol_click / 100;
    return sum;
}

void psg_sync(PSG *p, uint64_t now)
{
    for (;;) {
        uint64_t sample_t = p->next_sample_fp >> 16;
        uint64_t limit = sample_t < now ? sample_t : now;
        while (p->next_tick <= limit) {
            tick(p);
            p->acc += level(p);
            p->acc_n++;
            p->next_tick += T_PER_TICK;
        }
        if (sample_t > now) break;
        int32_t v = p->acc_n ? p->acc / p->acc_n : level(p);
        if (p->buf_n < PSG_FRAME_MAX) p->buf[p->buf_n++] = (int16_t)v;
        p->acc = 0;
        p->acc_n = 0;
        p->next_sample_fp += T_PER_SAMPLE_FP;
    }
}

void psg_write(PSG *p, uint8_t reg, uint8_t v)
{
    reg &= 0x0F;
    p->reg[reg] = v & reg_mask[reg];
    if (reg == 13) env_restart(p);
}

uint8_t psg_read_reg(const PSG *p, uint8_t reg)
{
    return p->reg[reg & 0x0F];
}

void psg_set_click(PSG *p, int on)
{
    p->click = (uint8_t)(on != 0);
}
