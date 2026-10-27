/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* ym64.c - 3.7.0 YM64 = YM2164(OPP) 기준 FM 음원 (3.4.1 DEV-32 YM2151(OPM)에서 이름·타이머 B를 바꿈, 설명은 ym64.h) */
#include "ym64.h"
#include "ym64_tab.h"
#include <string.h>

enum { ST_OFF, ST_ATT, ST_D1, ST_D2, ST_REL };
static const uint8_t eg_pat[4][8] = { { 0,1,0,1,0,1,0,1 }, { 0,1,0,1,1,1,0,1 }, { 0,1,1,1,0,1,1,1 }, { 0,1,1,1,1,1,1,1 } };
static const uint16_t pms_cents[8] = { 0, 5, 10, 20, 50, 100, 400, 700 };
static const uint16_t dt2_units[4] = { 0, 384, 500, 608 };   /* 1/768 옥타브 (= 1/64 반음) */

#define CH_REG(o, base, ch) ((o)->reg[(base) + (ch)])
#define OP_REG(o, base, s) ((o)->reg[(base) + (s)])

static int keycode(const YM64 *o, int ch) { return (CH_REG(o, 0x28, ch) >> 2) & 0x1F; }   /* 옥타브 × 4 + 음 위 2비트 */

/* 위상 증가: 반음 1/64 단위 S (A4 = 3584) → 32비트 위상 증가 */
static uint32_t inc_of(int32_t s)
{
    int32_t d = s - 3584;
    int oct = d >= 0 ? d / 768 : -((-d + 767) / 768);
    uint32_t frac = (uint32_t)(d - oct * 768);
    uint64_t v = (uint64_t)OPM_INC_A4 * opm_pow768[frac] >> 16;
    if (oct >= 0) v <<= oct; else v >>= -oct;
    return (uint32_t)v;
}

static void update_inc(YM64 *o, int ch)
{
    int kc = CH_REG(o, 0x28, ch) & 0x7F, n = kc & 15, idx = n - (n >> 2);
    int32_t s = ((kc >> 4) * 12 + idx) * 64 + (CH_REG(o, 0x30, ch) >> 2);
    int pms = (CH_REG(o, 0x38, ch) >> 4) & 7;
    if (pms) {
        int pmd = o->reg[0x1A] & 0x7F;                  /* PMD (0x19 비트 7 = 1로 쓴 값) */
        int32_t pm = o->lfo_pm * pmd >> 7;              /* -128~127 */
        s += pm * pms_cents[pms] * 64 / (128 * 100);
    }
    int kcode = keycode(o, ch);
    for (int k = 0; k < 4; k++) {
        int sl = k * 8 + ch;
        int dt2 = OP_REG(o, 0xC0, sl) >> 6, dt1 = (OP_REG(o, 0x40, sl) >> 4) & 7, mul = OP_REG(o, 0x40, sl) & 15;
        uint32_t inc = inc_of(s + dt2_units[dt2]);
        int32_t det = (int32_t)(dt1 & 3) * (kcode + 4) * 858;   /* DT1: 낮은 몇 Hz (건반이 높을수록 큼) */
        inc = (dt1 & 4) ? inc - (uint32_t)det : inc + (uint32_t)det;
        o->op[sl].inc = mul ? inc * (uint32_t)mul : inc >> 1;
    }
}

void opm_reset(YM64 *o)
{
    uint8_t opp = o->opp;                               /* 3.7.0: 칩 종류는 리셋해도 그대로 */
    memset(o, 0, sizeof *o);
    o->opp = opp;
    o->noise_lfsr = 1;
    for (int i = 0; i < 32; i++) o->op[i].env = 1023;
    for (int ch = 0; ch < 8; ch++) update_inc(o, ch);
}

static void key(YM64 *o, int sl, int on)
{
    OpmOp *p = &o->op[sl];
    if (on && !p->key) { p->state = ST_ATT; p->phase = 0; }
    else if (!on && p->key && p->state != ST_OFF) p->state = ST_REL;
    p->key = (uint8_t)on;
}

/* 타이머 B 한 칸: YM2151 = 1024클록, YM2164(OPP) = 2048클록 (타이머 B가 OPM의 절반 빠르기) */
static uint32_t tb_period(const YM64 *o) { return (o->opp ? 2048u : 1024u) * (256u - o->reg[0x12]); }

void opm_write(YM64 *o, int port, uint8_t v)
{
    if (!(port & 1)) { o->addr = v; return; }
    uint8_t r = o->addr;
    if (r == 0x19) { o->reg[(v & 0x80) ? 0x1A : 0x19] = v & 0x7F; o->inc_dirty = 1; return; }   /* 0x19 = AMD, 0x1A 자리에 PMD 보관 */
    if (r == 0x1A) return;
    uint8_t old = o->reg[r];
    o->reg[r] = v;
    if (r == 0x01 && (v & 0x02)) o->lfo_phase = 0;      /* LFO 리셋 */
    else if (r == 0x08) {                               /* 키 온: 비트 3 M1, 4 C1, 5 M2, 6 C2 */
        int ch = v & 7;
        key(o, ch, (v >> 3) & 1); key(o, 16 + ch, (v >> 4) & 1); key(o, 8 + ch, (v >> 5) & 1); key(o, 24 + ch, (v >> 6) & 1);
    } else if (r == 0x14) {
        if (v & 0x10) o->status &= (uint8_t)~1;
        if (v & 0x20) o->status &= (uint8_t)~2;
        if ((v & 1) && !(old & 1)) o->ta_cnt = 64u * (1024u - ((uint32_t)o->reg[0x10] << 2 | (o->reg[0x11] & 3)));
        if ((v & 2) && !(old & 2)) o->tb_cnt = tb_period(o);
    } else if ((r >= 0x28 && r < 0x40) || (r >= 0x40 && r < 0x60) || (r >= 0xC0 && r < 0xE0)) {
        update_inc(o, r < 0x40 ? (r & 7) : (r & 7));
    }
}

uint8_t opm_status(const YM64 *o) { return o->status; }

int opm_irq(const YM64 *o) { return ((o->status & 1) && (o->reg[0x14] & 0x04)) || ((o->status & 2) && (o->reg[0x14] & 0x08)); }

void opm_advance(YM64 *o, uint32_t clocks)
{
    uint8_t c = o->reg[0x14];
    if (c & 1) {
        uint32_t per = 64u * (1024u - ((uint32_t)o->reg[0x10] << 2 | (o->reg[0x11] & 3)));
        uint32_t left = clocks;
        while (left >= o->ta_cnt) {
            left -= o->ta_cnt; o->ta_cnt = per;
            if (c & 0x04) o->status |= 1;
            if (c & 0x80) for (int s = 0; s < 32; s++) { o->op[s].state = ST_ATT; o->op[s].phase = 0; }   /* CSM: 모든 슬롯 키 온 */
        }
        o->ta_cnt -= left;
    }
    if (c & 2) {
        uint32_t per = tb_period(o);
        uint32_t left = clocks;
        while (left >= o->tb_cnt) { left -= o->tb_cnt; o->tb_cnt = per; if (c & 0x08) o->status |= 2; }
        o->tb_cnt -= left;
    }
}

static int rate_of(const YM64 *o, int sl, int r5, int rr)
{
    int ks = OP_REG(o, 0x80, sl) >> 6, kcode = keycode(o, sl & 7);
    int base = rr ? r5 * 4 + 2 : r5 * 2;
    if (!r5) return 0;
    int r = base + (kcode >> (3 - ks));
    return r > 63 ? 63 : r;
}

static void eg_step(YM64 *o, OpmOp *p, int sl)
{
    int r;
    switch (p->state) {
    case ST_ATT: r = rate_of(o, sl, OP_REG(o, 0x80, sl) & 31, 0); break;
    case ST_D1: r = rate_of(o, sl, OP_REG(o, 0xA0, sl) & 31, 0); break;
    case ST_D2: r = rate_of(o, sl, OP_REG(o, 0xC0, sl) & 31, 0); break;
    case ST_REL: r = rate_of(o, sl, OP_REG(o, 0xE0, sl) & 15, 1); break;
    default: return;
    }
    if (p->state == ST_ATT && r >= 62) { p->env = 0; p->state = ST_D1; return; }
    if (r < 4) return;
    int shift = r < 44 ? 11 - r / 4 : 0;
    if (o->eg_cnt & ((1u << shift) - 1)) return;
    int inc = eg_pat[r & 3][(o->eg_cnt >> shift) & 7];
    if (r >= 44) inc = (eg_pat[r & 3][(o->eg_cnt) & 7] + 1) << (r / 4 - 11);
    if (!inc) return;
    if (p->state == ST_ATT) {
        p->env += (~p->env * inc) >> 4;
        if (p->env <= 0) { p->env = 0; p->state = ST_D1; }
        return;
    }
    p->env += inc;
    if (p->state == ST_D1) {
        int d1l = OP_REG(o, 0xE0, sl) >> 4;
        int lim = (d1l == 15 ? 31 : d1l) * 32;
        if (p->env >= lim) { p->state = ST_D2; }
    }
    if (p->env >= 1023) { p->env = 1023; if (p->state == ST_REL) p->state = ST_OFF; }
}

static void lfo_step(YM64 *o)
{
    uint8_t v = o->reg[0x18];
    uint32_t shift = (uint32_t)(255 - v) / 16, frac = (uint32_t)(255 - v) % 16 * 48;
    uint32_t inc = (uint32_t)(((uint64_t)4062000u << 16) / opm_pow768[frac]) >> shift;   /* 255 = 약 53 Hz */
    uint32_t before = o->lfo_phase >> 24;
    o->lfo_phase += inc;
    uint32_t p = o->lfo_phase >> 24;
    int am, pm;
    switch (o->reg[0x1B] & 3) {
    case 0: am = 255 - (int)p; pm = p < 128 ? (int)p : (int)p - 256; break;
    case 1: am = p < 128 ? 255 : 0; pm = p < 128 ? 127 : -128; break;
    case 2: { int t = p < 128 ? (int)p * 2 : 511 - (int)p * 2; am = 255 - t; pm = t - 128; break; }
    default:
        if (p != before) { o->noise_lfsr = (o->noise_lfsr >> 1) ^ (-(o->noise_lfsr & 1) & 0x12000u); o->lfo_noise = (uint8_t)o->noise_lfsr; }
        am = o->lfo_noise; pm = (int)o->lfo_noise - 128;
        break;
    }
    o->lfo_am = am * (o->reg[0x19] & 0x7F) >> 7;
    if (pm != o->lfo_pm || o->inc_dirty) {
        o->lfo_pm = pm; o->inc_dirty = 0;
        if (o->reg[0x1A]) for (int ch = 0; ch < 8; ch++) if (CH_REG(o, 0x38, ch) & 0x70) update_inc(o, ch);
    }
}

static int32_t op_out(YM64 *o, int sl, int32_t mod)
{
    OpmOp *p = &o->op[sl];
    int att = p->env + ((OP_REG(o, 0x60, sl) & 0x7F) << 3);
    if ((OP_REG(o, 0xA0, sl) & 0x80) && (CH_REG(o, 0x38, sl & 7) & 3)) att += o->lfo_am << ((CH_REG(o, 0x38, sl & 7) & 3) - 1);
    uint32_t idx = ((p->phase >> 22) + (uint32_t)mod) & 1023;
    p->phase += p->inc;
    if (att >= 1023 || p->state == ST_OFF) { p->out = 0; return 0; }
    int32_t v = (int32_t)opm_sin[idx] * opm_exp[att & 63] >> 16 >> (att >> 6);
    p->out = v;
    return v;
}

void opm_calc(YM64 *o, int32_t *l, int32_t *r)
{
    if (++o->eg_sub >= 3) {
        o->eg_sub = 0; o->eg_cnt++;
        for (int s = 0; s < 32; s++) eg_step(o, &o->op[s], s);
    }
    lfo_step(o);
    uint8_t ne = o->reg[0x0F];
    if (ne & 0x80) {
        if (++o->noise_cnt >= (uint32_t)(32 - (ne & 31)) + 0) {
            o->noise_cnt = 0;
            o->noise_lfsr = (o->noise_lfsr >> 1) ^ (-(o->noise_lfsr & 1) & 0x12000u);
        }
    }
    int32_t sl_ = 0, sr = 0;
    for (int ch = 0; ch < 8; ch++) {
        uint8_t c = CH_REG(o, 0x20, ch);
        int fl = (c >> 3) & 7, con = c & 7;
        int32_t fbm = fl ? (o->fb[ch][0] + o->fb[ch][1]) >> (10 - fl) : 0;
        int32_t m1 = op_out(o, ch, fbm);
        o->fb[ch][0] = o->fb[ch][1]; o->fb[ch][1] = m1;
        int32_t c1, m2, out;
        switch (con) {
        case 0: c1 = op_out(o, 16 + ch, m1 >> 1); m2 = op_out(o, 8 + ch, c1 >> 1); out = op_out(o, 24 + ch, m2 >> 1); break;
        case 1: c1 = op_out(o, 16 + ch, 0); m2 = op_out(o, 8 + ch, (m1 + c1) >> 1); out = op_out(o, 24 + ch, m2 >> 1); break;
        case 2: c1 = op_out(o, 16 + ch, 0); m2 = op_out(o, 8 + ch, c1 >> 1); out = op_out(o, 24 + ch, (m1 + m2) >> 1); break;
        case 3: c1 = op_out(o, 16 + ch, m1 >> 1); m2 = op_out(o, 8 + ch, 0); out = op_out(o, 24 + ch, (c1 + m2) >> 1); break;
        case 4: c1 = op_out(o, 16 + ch, m1 >> 1); m2 = op_out(o, 8 + ch, 0); out = c1 + op_out(o, 24 + ch, m2 >> 1); break;
        case 5: out = op_out(o, 16 + ch, m1 >> 1) + op_out(o, 8 + ch, m1 >> 1) + op_out(o, 24 + ch, m1 >> 1); break;
        case 6: out = op_out(o, 16 + ch, m1 >> 1) + op_out(o, 8 + ch, 0) + op_out(o, 24 + ch, 0); break;
        default: out = m1 + op_out(o, 16 + ch, 0) + op_out(o, 8 + ch, 0) + op_out(o, 24 + ch, 0); break;
        }
        if (ch == 7 && (ne & 0x80)) {                   /* 잡음: C2 자리 */
            OpmOp *p = &o->op[31];
            int att = p->env + ((OP_REG(o, 0x60, 31) & 0x7F) << 3);
            int32_t nz = (att >= 1023 || p->state == ST_OFF) ? 0 : ((o->noise_lfsr & 1) ? 8191 : -8191) * (int32_t)opm_exp[att & 63] >> 16 >> (att >> 6);
            out = out - o->op[31].out + nz;
        }
        if (c & 0x40) sl_ += out;
        if (c & 0x80) sr += out;
    }
    *l = sl_; *r = sr;
}

int opm_active(const YM64 *o)
{
    for (int s = 0; s < 32; s++) if (o->op[s].state != ST_OFF && o->op[s].env < 1000) return 1;
    return 0;
}
