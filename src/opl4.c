/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * opl4.c - YMF278B (OPL4) MoonSound (3.1.0). 설명은 opl4.h.
 *
 * FM(OPL3)은 실제 칩의 로그-사인 + 지수 표 방식(Nuked OPL3와 같은 수식)으로 연산자를 만들고,
 * 엔벨로프는 MAME fmopl 계열의 비율 표(eg_inc)로 49,716 Hz 한 샘플마다 움직인다.
 * 웨이브는 YMF278B 자료(openMSX와 같은 머리 12바이트 형식)를 따라 44.1 kHz로 샘플을 읽고 보간한다.
 */
#include "opl4.h"
#include "opl4_tab.h"
#include <string.h>

static uint8_t *wmem;
static uint32_t wrom;

void opl4_set_memory(uint8_t *mem, uint32_t rom_size) { wmem = mem; wrom = rom_size; }
int opl4_has_memory(void) { return wmem && wrom; }
uint8_t *opl4_memory(void) { return wmem; }

static uint8_t mem_rd(uint32_t a)
{
    a &= 0x3FFFFF;
    if (!wmem) return 0xFF;
    if (a >= OPL4_MEM_SIZE) return 0xFF;
    if (a < 0x200000) return a < wrom ? wmem[a] : 0xFF;
    return wmem[a];
}

/* ------------------------------------------------------------------ */
/* FM (OPL3)                                                           */
/* ------------------------------------------------------------------ */
static const uint8_t mt[16] = { 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30 };
static const uint8_t kslrom[16] = { 0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64 };
static const uint8_t kslshift[4] = { 8, 1, 2, 0 };
static const int8_t reg2op[32] = { 0, 1, 2, 3, 4, 5, -1, -1, 6, 7, 8, 9, 10, 11, -1, -1, 12, 13, 14, 15, 16, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static const uint8_t eg_inc[15][8] = {
    { 0, 1, 0, 1, 0, 1, 0, 1 }, { 0, 1, 0, 1, 1, 1, 0, 1 }, { 0, 1, 1, 1, 0, 1, 1, 1 }, { 0, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 2, 1, 1, 1, 2 }, { 1, 2, 1, 2, 1, 2, 1, 2 }, { 1, 2, 2, 2, 1, 2, 2, 2 },
    { 2, 2, 2, 2, 2, 2, 2, 2 }, { 2, 2, 2, 4, 2, 2, 2, 4 }, { 2, 4, 2, 4, 2, 4, 2, 4 }, { 2, 4, 4, 4, 2, 4, 4, 4 },
    { 4, 4, 4, 4, 4, 4, 4, 4 }, { 8, 8, 8, 8, 8, 8, 8, 8 }, { 0, 0, 0, 0, 0, 0, 0, 0 },
};

static int calc_exp(int level)
{
    if (level > 0x1FFF) level = 0x1FFF;
    return (exprom[level & 0xFF] << 1) >> (level >> 8);
}

/* 파형 8가지: phase 10비트, env 9비트 → 출력 약 ±4084 */
static int wave(int ws, int phase, int env)
{
    int neg = 0, out;
    phase &= 0x3FF;
    switch (ws & 7) {
    case 0:
        if (phase & 0x200) neg = 1;
        out = (phase & 0x100) ? logsinrom[(phase & 0xFF) ^ 0xFF] : logsinrom[phase & 0xFF];
        break;
    case 1:
        if (phase & 0x200) out = 0x1000;
        else out = (phase & 0x100) ? logsinrom[(phase & 0xFF) ^ 0xFF] : logsinrom[phase & 0xFF];
        break;
    case 2:
        out = (phase & 0x100) ? logsinrom[(phase & 0xFF) ^ 0xFF] : logsinrom[phase & 0xFF];
        break;
    case 3:
        out = (phase & 0x100) ? 0x1000 : logsinrom[phase & 0xFF];
        break;
    case 4:
        if ((phase & 0x300) == 0x100) neg = 1;
        if (phase & 0x200) out = 0x1000;
        else out = (phase & 0x80) ? logsinrom[((phase ^ 0xFF) << 1) & 0xFF] : logsinrom[(phase << 1) & 0xFF];
        break;
    case 5:
        if (phase & 0x200) out = 0x1000;
        else out = (phase & 0x80) ? logsinrom[((phase ^ 0xFF) << 1) & 0xFF] : logsinrom[(phase << 1) & 0xFF];
        break;
    case 6:
        if (phase & 0x200) neg = 1;
        out = 0;
        break;
    default:
        if (phase & 0x200) { neg = 1; phase = (phase & 0x1FF) ^ 0x1FF; }
        out = phase << 3;
        break;
    }
    int v = calc_exp(out + (env << 3));
    return neg ? -v : v;
}

static int is_new(const Opl4 *o) { return o->fmreg[0x105] & 1; }

static void op_keyon(Opl3Op *op, int bit)
{
    if (!op->key) { op->phase = 0; op->eg_state = 0; if (op->ar == 15) { op->eg_rout = 0; op->eg_state = 1; } }
    op->key |= (uint8_t)bit;
}
static void op_keyoff(Opl3Op *op, int bit)
{
    if (op->key) { op->key &= (uint8_t)~bit; if (!op->key) op->eg_state = 3; }
}

/* 채널 ch(0~17)의 연산자 두 개 */
static int ch_op(int ch, int k)
{
    int b = ch / 9, c = ch % 9;
    return b * 18 + (c % 3) + (c / 3) * 6 + k * 3;
}

static void eg_step(Opl4 *o, Opl3Op *op, const Opl3Ch *ch)
{
    int nts = (o->fmreg[0x08] >> 6) & 1;
    int ks = (ch->block << 1) | ((ch->fnum >> (9 - nts)) & 1);
    if (!op->ksr) ks >>= 2;
    int r;
    switch (op->eg_state) {
    case 0: r = op->ar; break;
    case 1: r = op->dr; break;
    case 2: r = op->egt ? 0 : op->rr; break;
    default: r = op->rr; break;
    }
    if (!r) return;
    int rate = r * 4 + ks;
    if (rate > 63) rate = 63;
    if (op->eg_state == 0 && rate >= 60) { op->eg_rout = 0; op->eg_state = 1; return; }
    int shift = rate < 48 ? 12 - (rate >> 2) : 0;
    int row = rate < 48 ? (rate & 3) : rate < 52 ? 4 + (rate & 3) : rate < 56 ? 8 + (rate & 3) : 12;
    if (o->eg_cnt & ((1u << shift) - 1)) return;
    int inc = eg_inc[row][(o->eg_cnt >> shift) & 7];
    int v = op->eg_rout;
    if (op->eg_state == 0) {
        v += ((~v) * inc) >> 3;
        if (v <= 0) { v = 0; op->eg_state = 1; }
    } else {
        v += inc;
        if (v > 511) v = 511;
        if (op->eg_state == 1) {
            int sl = op->sl == 15 ? 31 << 4 : op->sl << 4;
            if (v >= sl) op->eg_state = 2;
        }
    }
    op->eg_rout = (uint16_t)v;
}

static int op_env(const Opl4 *o, const Opl3Op *op, const Opl3Ch *ch)
{
    int ksl = (kslrom[ch->fnum >> 6] << 2) - ((8 - ch->block) << 5);
    if (ksl < 0) ksl = 0;
    int trem = o->trem_pos < 105 ? o->trem_pos : 210 - o->trem_pos;
    trem >>= (o->fmreg[0xBD] & 0x80) ? 2 : 4;
    int e = op->eg_rout + (op->tl << 2) + (ksl >> kslshift[op->ksl]) + (op->am ? trem : 0);
    return e > 511 ? 511 : e;
}

static void op_phase(Opl4 *o, Opl3Op *op, const Opl3Ch *ch)
{
    int f = ch->fnum;
    if (op->vib) {
        int range = (f >> 7) & 7, vp = o->vib_pos;
        if (!(vp & 3)) range = 0;
        else if (vp & 1) range >>= 1;
        if (!(o->fmreg[0xBD] & 0x40)) range >>= 1;
        if (vp & 4) range = -range;
        f += range;
    }
    uint32_t inc = ((uint32_t)((f << ch->block) >> 1) * mt[op->mult]) >> 1;
    op->phase = (op->phase + inc) & 0x7FFFF;
}

/* 연산자 하나: 위상·엔벨로프 진행 후 출력 */
static int op_out(Opl4 *o, Opl3Op *op, const Opl3Ch *ch, int mod, int phase_override)
{
    if (op->eg_state == 3 && op->eg_rout >= 511) { op_phase(o, op, ch); op->prout = op->out; op->out = 0; return 0; }   /* 꺼진 연산자는 계산을 건너뛰되 위상은 계속 돎 (리듬 하이햇·심벌 위상 비트가 쓰임 — 5.0.48) */
    eg_step(o, op, ch);
    int ph = phase_override >= 0 ? phase_override : (int)(op->phase >> 9);
    op_phase(o, op, ch);
    int ws = is_new(o) ? op->ws & 7 : op->ws & 3;
    op->prout = op->out;
    op->out = (int16_t)wave(ws, ph + mod, op_env(o, op, ch));
    return op->out;
}

static int fb_mod(const Opl3Op *op, int fb) { return fb ? (op->out + op->prout) >> (9 - fb) : 0; }

/* 한 FM 샘플 (49,716 Hz): 왼쪽·오른쪽 */
static void fm_sample(Opl4 *o, int *l, int *r)
{
    o->eg_cnt++;
    o->timer++;
    if ((o->timer & 0x3F) == 0) o->trem_pos = (uint8_t)((o->trem_pos + 1) % 210);
    if ((o->timer & 0x3FF) == 0) o->vib_pos = (uint8_t)((o->vib_pos + 1) & 7);
    /* 실제 칩(Nuked OPL3)처럼 잡음 레지스터는 슬롯마다(샘플당 36번) 움직이고, 하이햇은 13번째·스네어는 16번째 슬롯 시점의 값을 쓴다 */
    int nz13 = 0, nz16 = 0;
    { uint32_t n = o->noise; for (int i = 0; i < 36; i++) { if (i == 13) nz13 = (int)(n & 1); if (i == 16) nz16 = (int)(n & 1); uint32_t b = ((n >> 14) ^ n) & 1; n = (n >> 1) | (b << 22); } o->noise = n; }
    int L = 0, R = 0, nw = is_new(o);
    uint8_t con4 = nw ? o->fmreg[0x104] & 0x3F : 0;
    int rhythm = o->fmreg[0xBD] & 0x20;
    for (int c = 0; c < 18; c++) {
        Opl3Ch *ch = &o->ch[c];
        int b = c / 9, cc = c % 9, out = 0;
        /* 4연산자: 채널 0·1·2(뱅크마다)가 3·4·5를 끌고 간다 */
        if (cc < 6 && (con4 >> (b * 3 + cc % 3) & 1)) {
            if (cc >= 3) continue;
            Opl3Ch *ch2 = &o->ch[c + 3];
            Opl3Op *a = &o->op[ch_op(c, 0)], *bb = &o->op[ch_op(c, 1)], *cop = &o->op[ch_op(c + 3, 0)], *d = &o->op[ch_op(c + 3, 1)];
            int alg = (ch->cnt & 1) | ((ch2->cnt & 1) << 1);
            int o1 = op_out(o, a, ch, fb_mod(a, ch->fb), -1);
            switch (alg) {
            case 0: { int o2 = op_out(o, bb, ch, o1, -1); int o3 = op_out(o, cop, ch, o2, -1); out = op_out(o, d, ch, o3, -1); break; }
            case 1: { int o2 = op_out(o, bb, ch, 0, -1); int o3 = op_out(o, cop, ch, o2, -1); out = o1 + op_out(o, d, ch, o3, -1); break; }
            case 2: { int o2 = op_out(o, bb, ch, o1, -1); int o3 = op_out(o, cop, ch, 0, -1); out = o2 + op_out(o, d, ch, o3, -1); break; }
            default: { int o2 = op_out(o, bb, ch, 0, -1); int o3 = op_out(o, cop, ch, o2, -1); out = o1 + o3 + op_out(o, d, ch, 0, -1); break; }
            }
        } else if (rhythm && b == 0 && cc >= 6) {
            Opl3Op *m = &o->op[ch_op(c, 0)], *k = &o->op[ch_op(c, 1)];
            if (cc == 6) {                                  /* 베이스 드럼 */
                int o1 = op_out(o, m, ch, fb_mod(m, ch->fb), -1);
                out = 2 * op_out(o, k, ch, (ch->cnt & 1) ? 0 : o1, -1);
            } else if (cc == 7) {                           /* 하이햇(13) + 스네어(16) */
                uint32_t p13 = m->phase >> 9;
                int hb2 = (p13 >> 2) & 1, hb3 = (p13 >> 3) & 1, hb7 = (p13 >> 7) & 1, hb8 = (p13 >> 8) & 1;
                o->rm_hh_bit2 = (uint8_t)hb2; o->rm_hh_bit3 = (uint8_t)hb3; o->rm_hh_bit7 = (uint8_t)hb7;   /* 탐탐·심벌 슬롯이 이 샘플의 하이햇 위상 비트(증가 전)를 씀 */
                int tb3 = o->rm_tc_bit3, tb5 = o->rm_tc_bit5;       /* 심벌 위상 비트는 앞 샘플 값 (칩은 17번째 슬롯이 나중에 계산) */
                int x = (hb2 ^ hb7) | (hb3 ^ tb5) | (tb3 ^ tb5);
                int phh = (x << 9) | ((x ^ nz13) ? 0xD0 : 0x34);
                int psd = (hb8 << 9) | ((hb8 ^ nz16) << 8);
                out = 2 * (op_out(o, m, ch, 0, phh) + op_out(o, k, ch, 0, psd));
            } else {                                        /* 탐탐(14) + 심벌(17) */
                uint32_t p17 = k->phase >> 9;
                int hb2 = o->rm_hh_bit2, hb3 = o->rm_hh_bit3, hb7 = o->rm_hh_bit7;
                int tb3 = (p17 >> 3) & 1, tb5 = (p17 >> 5) & 1;
                o->rm_tc_bit3 = (uint8_t)tb3; o->rm_tc_bit5 = (uint8_t)tb5;
                int x = (hb2 ^ hb7) | (hb3 ^ tb5) | (tb3 ^ tb5);
                out = 2 * (op_out(o, m, ch, 0, -1) + op_out(o, k, ch, 0, (x << 9) | 0x80));
            }
        } else {
            Opl3Op *m = &o->op[ch_op(c, 0)], *k = &o->op[ch_op(c, 1)];
            int o1 = op_out(o, m, ch, fb_mod(m, ch->fb), -1);
            out = (ch->cnt & 1) ? o1 + op_out(o, k, ch, 0, -1) : op_out(o, k, ch, o1, -1);
        }
        int ol = nw ? ch->outl : 1, orr = nw ? ch->outr : 1;
        if (ol) L += out;
        if (orr) R += out;
    }
    /* 타이머 (80 µs·320 µs 단위 ≈ 4·16 샘플) */
    uint8_t t4 = o->fmreg[0x04];
    if (t4 & 1) { if (!o->t1_cnt) o->t1_cnt = (uint32_t)(256 - o->fmreg[0x02]) * 4; if (!--o->t1_cnt && !(t4 & 0x40)) o->status |= 0xC0; }
    if (t4 & 2) { if (!o->t2_cnt) o->t2_cnt = (uint32_t)(256 - o->fmreg[0x03]) * 16; if (!--o->t2_cnt && !(t4 & 0x20)) o->status |= 0xA0; }
    *l = L; *r = R;
}

static void fm_write(Opl4 *o, int bank, uint8_t a, uint8_t v)
{
    int r = bank * 256 + a;
    if (bank && a == 0x05) { if ((v & 2) && !o->new2_sig) { o->status2 = 0x02; o->new2_sig = 1; } o->fmreg[0x105] = v; return; }
    if (bank && a == 0x04) { o->fmreg[0x104] = v; return; }
    o->fmreg[r] = v;
    if (!bank && a == 0x04) {
        if (v & 0x80) { o->status = 0; return; }
        if (!(v & 1)) o->t1_cnt = 0;
        if (!(v & 2)) o->t2_cnt = 0;
        return;
    }
    int hi = a & 0xE0, lo = a & 0x1F;
    if (hi >= 0x20 && hi <= 0x80 && hi != 0xA0 && reg2op[lo] >= 0) {
        Opl3Op *op = &o->op[bank * 18 + reg2op[lo]];
        switch (hi) {
        case 0x20: op->am = v >> 7; op->vib = (v >> 6) & 1; op->egt = (v >> 5) & 1; op->ksr = (v >> 4) & 1; op->mult = v & 15; break;
        case 0x40: op->ksl = v >> 6; op->tl = v & 63; break;
        case 0x60: op->ar = v >> 4; op->dr = v & 15; break;
        case 0x80: op->sl = v >> 4; op->rr = v & 15; break;
        }
        return;
    }
    if (hi == 0xE0 && reg2op[lo] >= 0) { o->op[bank * 18 + reg2op[lo]].ws = v & 7; return; }
    if (a >= 0xA0 && a <= 0xA8) { o->ch[bank * 9 + a - 0xA0].fnum = (uint16_t)((o->ch[bank * 9 + a - 0xA0].fnum & 0x300) | v); return; }
    if (a >= 0xB0 && a <= 0xB8) {
        int c = bank * 9 + a - 0xB0;
        Opl3Ch *ch = &o->ch[c];
        ch->fnum = (uint16_t)((ch->fnum & 0xFF) | ((v & 3) << 8));
        ch->block = (v >> 2) & 7;
        uint8_t k = (v >> 5) & 1;
        int cc = a - 0xB0;
        int con4 = is_new(o) && cc < 6 && ((o->fmreg[0x104] >> (bank * 3 + cc % 3)) & 1);
        if (con4 && cc >= 3) { ch->kon = k; return; }       /* 4연산자 둘째 채널의 키는 첫 채널을 따름 */
        if (k && !ch->kon) {
            op_keyon(&o->op[ch_op(c, 0)], 1); op_keyon(&o->op[ch_op(c, 1)], 1);
            if (con4) { op_keyon(&o->op[ch_op(c + 3, 0)], 1); op_keyon(&o->op[ch_op(c + 3, 1)], 1); }
        } else if (!k && ch->kon) {
            op_keyoff(&o->op[ch_op(c, 0)], 1); op_keyoff(&o->op[ch_op(c, 1)], 1);
            if (con4) { op_keyoff(&o->op[ch_op(c + 3, 0)], 1); op_keyoff(&o->op[ch_op(c + 3, 1)], 1); }
        }
        ch->kon = k;
        if (con4) { o->ch[c + 3].fnum = ch->fnum; o->ch[c + 3].block = ch->block; }
        return;
    }
    if (a >= 0xC0 && a <= 0xC8) {
        Opl3Ch *ch = &o->ch[bank * 9 + a - 0xC0];
        ch->fb = (v >> 1) & 7; ch->cnt = v & 1; ch->outl = (v >> 4) & 1; ch->outr = (v >> 5) & 1;
        return;
    }
    if (!bank && a == 0xBD) {
        static const uint8_t rop[5][2] = { { 13, 13 }, { 17, 17 }, { 14, 14 }, { 16, 16 }, { 12, 15 } };   /* HH CY TOM SD BD */
        for (int i = 0; i < 5; i++) {
            int on = (v & 0x20) && (v >> i & 1);
            for (int k = 0; k < 2; k++) { if (on) op_keyon(&o->op[rop[i][k]], 2); else op_keyoff(&o->op[rop[i][k]], 2); }
        }
    }
}

/* ------------------------------------------------------------------ */
/* 웨이브 (PCM)                                                         */
/* ------------------------------------------------------------------ */
/* 이 아래는 openMSX YMF278.cc (실제 칩과 대조해 확인한 동작) 를 정수로 옮긴 것이다. */
#define W_MAX_ATT 0x280
#define W_LFO_PERIOD (1u << 18)
static const uint8_t pan_left[16]  = { 0, 8, 16, 24, 32, 40, 48, 255, 255, 0, 0, 0, 0, 0, 0, 0 };
static const uint8_t pan_right[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 255, 255, 48, 40, 32, 24, 16, 8 };
static const int16_t w_dl_tab[16] = { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 992 };
static const uint8_t w_eg_inc[15 * 8] = {
    0,1,0,1,0,1,0,1, 0,1,0,1,1,1,0,1, 0,1,1,1,0,1,1,1, 0,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1, 1,1,1,2,1,1,1,2, 1,2,1,2,1,2,1,2, 1,2,2,2,1,2,2,2,
    2,2,2,2,2,2,2,2, 2,2,2,4,2,2,2,4, 2,4,2,4,2,4,2,4, 2,4,4,4,2,4,4,4,
    4,4,4,4,4,4,4,4, 8,8,8,8,8,8,8,8, 0,0,0,0,0,0,0,0,
};
static uint8_t w_eg_sel[64], w_eg_shift[64];
static uint32_t w_lfo_period[8];
static const int16_t w_vib_depth[8] = { 0, 2, 3, 4, 6, 12, 24, 48 };
static const uint8_t w_am_depth[8] = { 0x00, 0x14, 0x20, 0x28, 0x30, 0x40, 0x50, 0x80 };
static int w_tab_ready;

static void w_tab_init(void)
{
    if (w_tab_ready) return;
    w_tab_ready = 1;
    for (int i = 0; i < 64; i++) {
        int sel = i < 4 ? 14 : i < 52 ? (i & 3) : i < 56 ? 4 + (i & 3) : i < 60 ? 8 + (i & 3) : 12;
        w_eg_sel[i] = (uint8_t)(sel * 8);
        w_eg_shift[i] = i < 4 ? 12 : i < 48 ? (uint8_t)(12 - (i >> 2)) : 0;
    }
    static const uint8_t st[8] = { 1, 12, 19, 25, 31, 35, 37, 42 };   /* LFO 한 샘플당 걸음 (0.168~7.066 Hz, openMSX 표) — 커널은 실수 연산 불가 */
    for (int i = 0; i < 8; i++) w_lfo_period[i] = st[i];
}

static int w_gain(int units)                                   /* 0.09375 dB 단위 → 0~2042 (믹스 레벨용) */
{
    int l = units * 4;
    if (l >= 0x1000) return 0;
    return exprom[l & 0xFF] >> (l >> 8);
}

static int16_t w_sample(const Opl4Slot *s, uint16_t pos)
{
    switch (s->bits) {
    case 0: return (int16_t)(mem_rd(s->start + pos) << 8);
    case 1: {
        uint32_t a = s->start + ((uint32_t)pos / 2) * 3;
        if (pos & 1) return (int16_t)((mem_rd(a + 2) << 8) | (mem_rd(a + 1) & 0xF0));
        return (int16_t)((mem_rd(a) << 8) | ((mem_rd(a + 1) << 4) & 0xF0));
    }
    case 2: { uint32_t a = s->start + (uint32_t)pos * 2; return (int16_t)((mem_rd(a) << 8) | mem_rd(a + 1)); }
    default: return 0;
    }
}

static uint16_t w_next_pos(const Opl4Slot *s, uint16_t pos, uint16_t inc)
{
    pos = (uint16_t)(pos + inc);
    if ((uint32_t)pos + s->end >= 0x10000u) pos = (uint16_t)(pos + s->end + s->loop);
    return pos;
}

static uint32_t w_calc_step(int oct, int fn, int vib)
{
    if (oct == -8) return 0;
    uint32_t t = (uint32_t)(fn + 1024 + vib) << (8 + oct);
    return t >> 3;
}

static int w_rate(const Opl4Slot *s, int val)
{
    if (!val) return 0;
    if (val == 15) return 63;
    int res = val * 4;
    if (s->rc != 15) {
        int k = s->oct + s->rc; k = k < 0 ? 0 : k > 15 ? 15 : k;
        res += 2 * k + ((s->fn & 0x200) ? 1 : 0);
    }
    return res < 0 ? 0 : res > 63 ? 63 : res;
}

static int w_decay_rate(const Opl4Slot *s, int val)
{
    if (s->damp) return s->env < w_dl_tab[4] ? 48 : 63;
    if (s->prvb && s->env >= w_dl_tab[6]) return 20;
    return w_rate(s, val);
}

static int w_vib(const Opl4Slot *s)
{
    int lfo_fm = (int)(s->lfo_cnt / (W_LFO_PERIOD / 0x40));
    if (lfo_fm & 0x10) lfo_fm ^= 0x1F;
    if (lfo_fm & 0x20) lfo_fm = -(lfo_fm & 0x0F);
    return (lfo_fm * w_vib_depth[s->vib]) / 12;
}

static int w_am(const Opl4Slot *s)
{
    int lfo_am = (int)(s->lfo_cnt / (W_LFO_PERIOD / 0x100));
    if (lfo_am >= 0x80) lfo_am ^= 0xFF;
    return (lfo_am * w_am_depth[s->am]) >> 7;
}

static int w_vol_factor(int x, unsigned env)
{
    if (env >= W_MAX_ATT) return 0;
    int mul = 0x80 - (int)(env & 0x3F), sh = 7 + (int)(env >> 6);
    return (x * ((0x8000 * mul) >> sh)) >> 15;
}

static void w_keyon(Opl4Slot *s)
{
    s->env = W_MAX_ATT;
    if (w_rate(s, s->ar) < 63) s->state = 4;
    else { s->env = 0; s->state = s->dl ? 3 : 2; }
    s->step_ptr = 0; s->pos = 0;
}

static void w_load_header(Opl4 *o, Opl4Slot *s);
static void w_write(Opl4 *o, uint8_t r, uint8_t v);

static void w_advance(Opl4 *o)
{
    uint32_t eg = ++o->weg_cnt;
    uint32_t tl_cnt = eg % 9, tl_step = (eg / 9) % 3;
    for (int i = 0; i < 24; i++) {
        Opl4Slot *s = &o->sl[i];
        if (tl_cnt == 0) {                              /* 음량 변경은 천천히 따라감 (딸깍 소리 방지) */
            if (tl_step == 0) { if (s->tl < s->tldest) s->tl++; }
            else { if (s->tl > s->tldest) s->tl--; }
        }
        if (s->lfo_active) s->lfo_cnt = (s->lfo_cnt + w_lfo_period[s->lfo]) & (W_LFO_PERIOD - 1);
        switch (s->state) {
        case 4: {
            int rate = w_rate(s, s->ar);
            if (rate >= 63) break;
            int sh = w_eg_shift[rate];
            if (!(eg & ((1u << sh) - 1))) {
                s->env = (int16_t)(s->env + ((~s->env * w_eg_inc[w_eg_sel[rate] + ((eg >> sh) & 7)]) >> 4));
                if (s->env <= 0) { s->env = 0; s->state = s->dl ? 3 : 2; }
            }
            break;
        }
        case 3: {
            int rate = w_decay_rate(s, s->d1r), sh = w_eg_shift[rate];
            if (!(eg & ((1u << sh) - 1))) {
                s->env = (int16_t)(s->env + w_eg_inc[w_eg_sel[rate] + ((eg >> sh) & 7)]);
                if (s->env >= s->dl) s->state = s->env < W_MAX_ATT ? 2 : 0;
            }
            break;
        }
        case 2: case 1: {
            int rate = w_decay_rate(s, s->state == 2 ? s->d2r : s->rr), sh = w_eg_shift[rate];
            if (!(eg & ((1u << sh) - 1))) {
                s->env = (int16_t)(s->env + w_eg_inc[w_eg_sel[rate] + ((eg >> sh) & 7)]);
                if (s->env >= W_MAX_ATT) { s->env = W_MAX_ATT; s->state = 0; }
            }
            break;
        }
        default: break;
        }
    }
}

static void pcm_sample(Opl4 *o, int *l, int *r)
{
    int L = 0, R = 0;
    for (int i = 0; i < 24; i++) {
        Opl4Slot *s = &o->sl[i];
        if (s->state == 0) continue;
        int smp = (int16_t)(((int)w_sample(s, s->pos) * (int)(0x10000 - s->step_ptr) +
                             (int)w_sample(s, w_next_pos(s, s->pos, 1)) * (int)s->step_ptr) >> 16);
        int env = s->env + ((s->lfo_active && s->am) ? w_am(s) : 0);
        if (env > W_MAX_ATT) env = W_MAX_ATT;
        int out = w_vol_factor(w_vol_factor(smp, (unsigned)env), (unsigned)s->tl << 2);
        int vl = pan_left[s->pan], vr = pan_right[s->pan];
        vl = (0x20 - (vl & 0x0F)) >> (vl >> 4);
        vr = (0x20 - (vr & 0x0F)) >> (vr >> 4);
        L += (out * vl) >> 5;
        R += (out * vr) >> 5;
        uint32_t step = (s->lfo_active && s->vib) ? w_calc_step(s->oct, s->fn, w_vib(s)) : s->step;
        s->step_ptr += step;
        if (s->step_ptr >= 0x10000u) {
            s->pos = w_next_pos(s, s->pos, (uint16_t)(s->step_ptr >> 16));
            s->step_ptr &= 0xFFFF;
        }
    }
    w_advance(o);
    *l = L; *r = R;
}

static void w_load_header(Opl4 *o, Opl4Slot *s)
{
    int hdr = (o->wreg[2] >> 2) & 7;
    uint32_t base = (s->wave < 384 || !hdr) ? (uint32_t)s->wave * 12 : ((uint32_t)hdr * 0x80000u) + (uint32_t)(s->wave - 384) * 12;
    uint8_t h[12];
    for (int i = 0; i < 12; i++) h[i] = mem_rd(base + (uint32_t)i);
    int slot = (int)(s - o->sl);
    s->bits = (h[0] & 0xC0) >> 6;
    s->start = (uint32_t)h[2] | ((uint32_t)h[1] << 8) | ((uint32_t)(h[0] & 0x3F) << 16);
    s->loop = (uint16_t)(h[4] | (h[3] << 8));
    s->end = (uint16_t)(h[6] | (h[5] << 8));
    for (int i = 7; i < 12; i++) w_write(o, (uint8_t)(8 + slot + (i - 2) * 24), h[i]);
    if (s->keyon) w_keyon(s);
    else { s->step_ptr = 0; s->pos = 0; }
}

static void w_write(Opl4 *o, uint8_t r, uint8_t v)
{
    if (r >= 0x08 && r < 0xF8) {
        int slot = (r - 0x08) % 24, grp = (r - 0x08) / 24;
        Opl4Slot *s = &o->sl[slot];
        switch (grp) {
        case 0: s->wave = (uint16_t)((s->wave & 0x100) | v); o->wreg[r] = v; w_load_header(o, s); return;
        case 1: s->wave = (uint16_t)((s->wave & 0xFF) | ((v & 1) << 8)); s->fn = (uint16_t)((s->fn & 0x380) | (v >> 1)); s->step = w_calc_step(s->oct, s->fn, 0); break;
        case 2: s->fn = (uint16_t)((s->fn & 0x07F) | ((v & 7) << 7)); s->prvb = (v >> 3) & 1; s->oct = (int8_t)(((v >> 4) ^ 8) - 8); s->step = w_calc_step(s->oct, s->fn, 0); break;
        case 3: { uint8_t t = v >> 1; s->tldest = t != 0x7F ? t : 0xFF; if (v & 1) s->tl = s->tldest; break; }
        case 4:
            s->pan = (v & 0x10) ? 8 : (v & 0x0F);
            if (v & 0x20) { s->lfo_active = 0; s->lfo_cnt = 0; } else s->lfo_active = 1;
            s->damp = (v >> 6) & 1;
            if (v & 0x80) { if (!s->keyon) { s->keyon = 1; w_keyon(s); } }
            else if (s->keyon) { s->keyon = 0; s->state = 1; }
            break;
        case 5: s->lfo = (v >> 3) & 7; s->vib = v & 7; break;
        case 6: s->ar = v >> 4; s->d1r = v & 15; break;
        case 7: s->dl = w_dl_tab[v >> 4]; s->d2r = v & 15; break;
        case 8: s->rc = v >> 4; s->rr = v & 15; break;
        case 9: s->am = v & 7; break;
        }
        o->wreg[r] = v;
        return;
    }
    switch (r) {
    case 0x03: v &= 0x3F; break;                                    /* 읽으면 위 두 비트는 늘 0 */
    case 0x05: o->memaddr = ((uint32_t)o->wreg[3] << 16) | ((uint32_t)o->wreg[4] << 8) | v; break;   /* 실제 칩: 5번에서만 주소가 바뀜 */
    case 0x06:
        if (o->wreg[2] & 1) {                                       /* 실제 칩: 비트 0 이 꺼져 있으면 쓰기 무시, 주소도 안 늘어남 */
            if (wmem && o->memaddr >= 0x200000 && o->memaddr < OPL4_MEM_SIZE) wmem[o->memaddr] = v;   /* 램만 */
            o->memaddr = (o->memaddr + 1) & 0x3FFFFF;
        }
        break;
    }
    o->wreg[r] = v;
}

/* ------------------------------------------------------------------ */
/* 바깥                                                                 */
/* ------------------------------------------------------------------ */
void opl4_reset(Opl4 *o)
{
    memset(o, 0, sizeof *o);
    for (int i = 0; i < 36; i++) { o->op[i].eg_rout = 511; o->op[i].eg_state = 3; }
    w_tab_init();
    for (int i = 0; i < 24; i++) { o->sl[i].env = W_MAX_ATT; o->sl[i].state = 0; o->sl[i].pan = 0; }
    o->noise = 1;
}

void opl4_write_fm(Opl4 *o, int port, uint8_t v)
{
    if (!(port & 1)) { int ab = (port >> 1) & 1; o->addr[ab] = v; o->lbank = (uint8_t)ab; return; }   /* 주소 포트: 뱅크 기억 */
    int bank = o->lbank & 1;                                       /* 데이터는 포트(C5/C7)와 상관없이 마지막 주소 뱅크로 (Z1 Drift 가 C6=05, C5=03 으로 NEW2 를 켬) */
    if (bank && !is_new(o) && o->addr[1] != 0x05) bank = 0;       /* NEW = 0이면 뱅크 1 쓰기는 뱅크 0으로 */
    fm_write(o, bank, o->addr[bank], v);
}

uint8_t opl4_read_fm(Opl4 *o, int port)
{
    if (!(port & 1)) { uint8_t r = (uint8_t)(o->status | o->status2); o->status2 = 0; return r; }   /* 비트 0 BUSY·비트 1 LD는 늘 0 (NEW2 처음 켠 뒤 한 번만 비트 1) */
    return 0xFF;
}

void opl4_write_wave(Opl4 *o, int port, uint8_t v)
{
    if (!(o->fmreg[0x105] & 2)) return;                           /* NEW2가 꺼져 있으면 웨이브 쪽은 안 보임 */
    if (!port) { o->waddr = v; return; }
    w_write(o, o->waddr, v);
}

uint8_t opl4_read_wave(Opl4 *o, int port)
{
    if (!(o->fmreg[0x105] & 2)) return 0xFF;
    if (!port) return 0xFF;
    uint8_t r = o->waddr;
    if (r == 0x02) return (uint8_t)((o->wreg[2] & 0x1F) | 0x20);  /* 장치 번호 1 */
    if (r == 0x06) { uint8_t v = mem_rd(o->memaddr); o->memaddr = (o->memaddr + 1) & 0x3FFFFF; return v; }
    return o->wreg[r];
}

static const uint8_t mixatt[8] = { 0, 8, 16, 24, 32, 40, 48, 255 };     /* 3 dB 단위 (0.375 dB × 8), 7 = 끔 */
static int mix_apply(int v, int lvl)
{
    if (mixatt[lvl] == 255) return 0;
    return (int)(((int64_t)v * w_gain(mixatt[lvl] * 4)) >> 11);
}

int opl4_calc48(Opl4 *o)
{
    /* 직선 보간으로 48 kHz 로 (전에는 가장 가까운 샘플을 그대로 써서 높은 소리에 거친 잡음이 섞였음) */
    o->fm_acc += 49716;
    while (o->fm_acc >= 48000) {
        o->fm_acc -= 48000;
        int l, r; fm_sample(o, &l, &r);
        uint8_t m = o->wreg[0xF8];
        o->fm_prev = o->fm_last;
        o->fm_last = (mix_apply(l, m & 7) + mix_apply(r, (m >> 3) & 7));
    }
    o->pcm_acc += 44100;
    while (o->pcm_acc >= 48000) {
        o->pcm_acc -= 48000;
        int l, r; pcm_sample(o, &l, &r);
        uint8_t m = o->wreg[0xF9];
        o->pcm_prev = o->pcm_last;
        o->pcm_last = (mix_apply(l, m & 7) + mix_apply(r, (m >> 3) & 7)) / 2;
    }
    int fm = o->fm_prev + (int)(((int64_t)(o->fm_last - o->fm_prev) * (int64_t)o->fm_acc) / 48000);
    int pcm = o->pcm_prev + (int)(((int64_t)(o->pcm_last - o->pcm_prev) * (int64_t)o->pcm_acc) / 48000);
    /* openMSX 와 같은 음원 간 비율 (PSG 대비 FM 0.81, PCM 0.61 — 전에는 1.17 / 3.5) */
    return fm * 69 / 100 + pcm * 17 / 100;
}

int opl4_active(const Opl4 *o)
{
    for (int i = 0; i < 36; i++) if (o->op[i].key && o->op[i].eg_rout < 400) return 1;
    for (int i = 0; i < 24; i++) if (o->sl[i].state) return 1;
    return 0;
}
