/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mvflac.c (8.0.66) - FLAC 프레임 해독기. 설명은 mvflac.h. 외부 코드 없이 규격(RFC 9639)대로 직접 만듦. */
#include "mvflac.h"
#include <string.h>

typedef struct { const uint8_t *b; uint32_t n, pos; int bit; int over; } Br;   /* pos = 바이트, bit = 그 바이트에서 쓴 비트 수 */

static uint32_t br_bits(Br *r, int k)            /* k ≤ 32. 모자라면 over 를 세우고 0 */
{
    uint32_t v = 0;
    while (k > 0) {
        if (r->pos >= r->n) { r->over = 1; return 0; }
        int avail = 8 - r->bit, t = k < avail ? k : avail;
        uint32_t cur = (r->b[r->pos] >> (avail - t)) & ((1u << t) - 1u);
        v = (t == 32) ? cur : ((v << t) | cur);
        r->bit += t; k -= t;
        if (r->bit == 8) { r->bit = 0; r->pos++; }
    }
    return v;
}
static int32_t br_sbits(Br *r, int k) { if (k <= 0) return 0; uint32_t v = br_bits(r, k); return k >= 32 ? (int32_t)v : (int32_t)(v << (32 - k)) >> (32 - k); }
static uint32_t br_unary(Br *r)                  /* 1 이 나올 때까지 센 0 의 수 (FLAC 규약) */
{
    uint32_t q = 0;
    while (!r->over) { if (!br_bits(r, 1)) q++; else break; if (q > (1u << 24)) { r->over = 2; break; } }
    return q;
}

static uint8_t crc8(const uint8_t *p, uint32_t n)
{
    uint8_t c = 0;
    while (n--) { c ^= *p++; for (int i = 0; i < 8; i++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1); }
    return c;
}

int mvflac_info(FlacInfo *fi, const uint8_t *b)
{
    fi->min_bs = (b[0] << 8) | b[1]; fi->max_bs = (b[2] << 8) | b[3];
    fi->min_fs = (b[4] << 16) | (b[5] << 8) | b[6]; fi->max_fs = (b[7] << 16) | (b[8] << 8) | b[9];
    fi->sr = ((uint32_t)b[10] << 12) | (b[11] << 4) | (b[12] >> 4);
    fi->ch = ((b[12] >> 1) & 7) + 1;
    fi->bps = (((b[12] & 1) << 4) | (b[13] >> 4)) + 1;
    fi->total = ((uint64_t)(b[13] & 15) << 32) | ((uint32_t)b[14] << 24) | (b[15] << 16) | (b[16] << 8) | b[17];
    if (fi->ch > 2 || fi->bps < 4 || fi->bps > 24 || fi->sr == 0 || fi->max_bs > MVFLAC_MAXBS || fi->max_bs < 16) return -1;
    return 0;
}

int mvflac_header(const FlacInfo *fi, const uint8_t *b, uint32_t n, FlacHdr *h)
{
    if (n < 5) return 0;
    if (b[0] != 0xFF || (b[1] & 0xFE) != 0xF8) return -1;
    h->var = b[1] & 1;
    int bsc = b[2] >> 4, src = b[2] & 15, asg = b[3] >> 4, szc = (b[3] >> 1) & 7;
    if (b[3] & 1) return -1;
    uint32_t p = 4;
    uint8_t c0 = b[p];
    int ln = c0 < 0x80 ? 1 : (c0 & 0xE0) == 0xC0 ? 2 : (c0 & 0xF0) == 0xE0 ? 3 : (c0 & 0xF8) == 0xF0 ? 4 : (c0 & 0xFC) == 0xF8 ? 5 : (c0 & 0xFE) == 0xFC ? 6 : c0 == 0xFE ? 7 : 0;
    if (!ln) return -1;
    if (n < p + ln) return 0;
    uint64_t num = ln == 1 ? c0 : (uint64_t)(c0 & (0x7F >> ln));
    for (int i = 1; i < ln; i++) { if ((b[p + i] & 0xC0) != 0x80) return -1; num = (num << 6) | (b[p + i] & 0x3F); }
    p += ln;
    h->num = num;
    uint32_t bs;
    if (bsc == 0) return -1;
    else if (bsc == 1) bs = 192;
    else if (bsc <= 5) bs = 576u << (bsc - 2);
    else if (bsc == 6) { if (n < p + 1) return 0; bs = b[p] + 1u; p += 1; }
    else if (bsc == 7) { if (n < p + 2) return 0; bs = ((b[p] << 8) | b[p + 1]) + 1u; p += 2; }
    else bs = 256u << (bsc - 8);
    uint32_t sr;
    switch (src) {
    case 0: sr = fi->sr; break;
    case 1: sr = 88200; break; case 2: sr = 176400; break; case 3: sr = 192000; break; case 4: sr = 8000; break;
    case 5: sr = 16000; break; case 6: sr = 22050; break; case 7: sr = 24000; break; case 8: sr = 32000; break;
    case 9: sr = 44100; break; case 10: sr = 48000; break; case 11: sr = 96000; break;
    case 12: if (n < p + 1) return 0; sr = b[p] * 1000u; p += 1; break;
    case 13: if (n < p + 2) return 0; sr = (b[p] << 8) | b[p + 1]; p += 2; break;
    case 14: if (n < p + 2) return 0; sr = ((b[p] << 8) | b[p + 1]) * 10u; p += 2; break;
    default: return -1;
    }
    int bps;
    switch (szc) { case 0: bps = fi->bps; break; case 1: bps = 8; break; case 2: bps = 12; break; case 4: bps = 16; break; case 5: bps = 20; break; case 6: bps = 24; break; default: return -1; }
    if (asg > 10 || (asg >= 11)) return -1;
    if (n < p + 1) return 0;
    if (crc8(b, p) != b[p]) return -1;
    p++;
    h->bs = bs; h->sr = sr; h->bps = bps; h->assign = asg; h->nch = asg < 8 ? asg + 1 : 2;
    h->len = p;
    if (bs > MVFLAC_MAXBS || h->nch > 2 || bps > 24 || bps < 4) return -1;
    return (int)p;
}

/* 부분 합 잔차 → out[order..bs) 에 더해 놓기 위해 잔차만 먼저 out 에 */
static int residual(Br *r, int32_t *out, uint32_t bs, uint32_t order)
{
    uint32_t method = br_bits(r, 2);
    if (method > 1) return -1;
    uint32_t po = br_bits(r, 4);
    uint32_t parts = 1u << po;
    if ((bs >> po) < order || (bs & (parts - 1))) return -1;
    uint32_t n = bs >> po, idx = order;
    int pbits = method ? 5 : 4;
    uint32_t esc = method ? 31 : 15;
    for (uint32_t p = 0; p < parts; p++) {
        uint32_t cnt = p ? n : n - order;
        uint32_t k = br_bits(r, pbits);
        if (r->over) return 0;
        if (k == esc) {
            int rb = (int)br_bits(r, 5);
            for (uint32_t i = 0; i < cnt; i++) out[idx++] = br_sbits(r, rb);
        } else {
            for (uint32_t i = 0; i < cnt; i++) {
                uint32_t q = br_unary(r);
                uint32_t v = (q << k) | (k ? br_bits(r, (int)k) : 0);
                out[idx++] = (int32_t)(v >> 1) ^ -(int32_t)(v & 1);
                if (r->over) return 0;
            }
        }
        if (r->over) return 0;
    }
    return 1;
}

static int subframe(Br *r, int32_t *out, uint32_t bs, int bps)
{
    if (br_bits(r, 1)) return -1;
    uint32_t type = br_bits(r, 6);
    int wasted = 0;
    if (br_bits(r, 1)) { wasted = 1; while (!r->over && !br_bits(r, 1)) wasted++; }
    if (r->over) return 0;
    bps -= wasted;
    if (bps < 1 || bps > 32) return -1;
    if (type == 0) {
        int32_t v = br_sbits(r, bps);
        for (uint32_t i = 0; i < bs; i++) out[i] = v;
    } else if (type == 1) {
        for (uint32_t i = 0; i < bs; i++) out[i] = br_sbits(r, bps);
    } else if (type >= 8 && type <= 12) {
        uint32_t o = type & 7;
        if (o > 4 || o > bs) return -1;
        for (uint32_t i = 0; i < o; i++) out[i] = br_sbits(r, bps);
        int rr = residual(r, out, bs, o);
        if (rr <= 0) return rr;
        for (uint32_t i = o; i < bs; i++) {
            int64_t p = 0;
            switch (o) {
            case 1: p = out[i - 1]; break;
            case 2: p = 2 * (int64_t)out[i - 1] - out[i - 2]; break;
            case 3: p = 3 * (int64_t)out[i - 1] - 3 * (int64_t)out[i - 2] + out[i - 3]; break;
            case 4: p = 4 * (int64_t)out[i - 1] - 6 * (int64_t)out[i - 2] + 4 * (int64_t)out[i - 3] - out[i - 4]; break;
            }
            out[i] = (int32_t)(p + out[i]);
        }
    } else if (type >= 32) {
        uint32_t o = (type & 31) + 1;
        if (o > bs) return -1;
        for (uint32_t i = 0; i < o; i++) out[i] = br_sbits(r, bps);
        uint32_t prec = br_bits(r, 4) + 1;
        if (prec == 16) return -1;
        int shift = br_sbits(r, 5);
        if (shift < 0) return -1;
        int32_t co[32];
        for (uint32_t i = 0; i < o; i++) co[i] = br_sbits(r, (int)prec);
        if (r->over) return 0;
        int rr = residual(r, out, bs, o);
        if (rr <= 0) return rr;
        for (uint32_t i = o; i < bs; i++) {
            int64_t s = 0;
            for (uint32_t j = 0; j < o; j++) s += (int64_t)co[j] * out[i - 1 - j];
            out[i] = (int32_t)((s >> shift) + out[i]);
        }
    } else return -1;
    if (wasted) for (uint32_t i = 0; i < bs; i++) out[i] = (int32_t)((uint32_t)out[i] << wasted);
    return r->over ? 0 : 1;
}

int mvflac_frame(const FlacInfo *fi, const uint8_t *b, uint32_t n, int32_t *out0, int32_t *out1, FlacHdr *h)
{
    int hl = mvflac_header(fi, b, n, h);
    if (hl <= 0) return hl;
    Br r = { b, n, (uint32_t)hl, 0, 0 };
    int32_t *o[2] = { out0, out1 };
    for (int c = 0; c < h->nch; c++) {
        int bps = h->bps + ((h->assign == 8 && c == 1) || (h->assign == 9 && c == 0) || (h->assign == 10 && c == 1) ? 1 : 0);
        int rr = subframe(&r, o[c], h->bs, bps);
        if (rr <= 0) return rr;
    }
    if (r.bit) { r.bit = 0; r.pos++; }
    if (r.pos + 2 > n) return 0;
    r.pos += 2;                                     /* 프레임 CRC-16: 검사 안 함 */
    if (h->assign == 8) { for (uint32_t i = 0; i < h->bs; i++) out1[i] = out0[i] - out1[i]; }
    else if (h->assign == 9) { for (uint32_t i = 0; i < h->bs; i++) out0[i] = out0[i] + out1[i]; }
    else if (h->assign == 10) {
        for (uint32_t i = 0; i < h->bs; i++) {
            int32_t m = out0[i], s = out1[i];
            m = (int32_t)(((uint32_t)m << 1) | ((uint32_t)s & 1));
            out0[i] = (m + s) >> 1; out1[i] = (m - s) >> 1;
        }
    }
    return (int)r.pos;
}
