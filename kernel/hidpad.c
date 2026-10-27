/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* hidpad.c - USB 게임패드 보고 해석 (2.13). 참고: HID 1.11 §6.2.2 */
#include "hidpad.h"
#include <string.h>

static int32_t sval(uint32_t v, int n)              /* n바이트 값을 부호 있게 */
{
    if (n == 1) return (int8_t)v;
    if (n == 2) return (int16_t)v;
    return (int32_t)v;
}

int hidpad_parse(const uint8_t *d, int len, HidPad *p)
{
    memset(p, 0, sizeof *p);
    uint32_t page = 0, rsize = 0, rcount = 0;
    int32_t lmin = 0, lmax = 0;
    uint32_t usages[32]; int nu = 0;
    uint32_t umin = 0, umax = 0; int has_range = 0;
    int is_pad = 0, depth = 0, pad_depth = -1;
    uint8_t cur_id = 0;
    uint32_t bitpos[256];                           /* 보고 번호별 입력 비트 위치 */
    memset(bitpos, 0, sizeof bitpos);
    int got_id = -1;                                /* 게임패드 필드를 찾은 보고 번호 */

    for (int i = 0; i < len; ) {
        uint8_t b = d[i++];
        if (b == 0xFE) { if (i + 1 < len) i += 2 + d[i]; else break; continue; }   /* 긴 항목 */
        int n = b & 3; if (n == 3) n = 4;
        if (i + n > len) break;
        uint32_t v = 0;
        for (int k = 0; k < n; k++) v |= (uint32_t)d[i + k] << (8 * k);
        i += n;
        int type = (b >> 2) & 3, tag = b >> 4;
        if (type == 1) {                            /* 전역 */
            switch (tag) {
            case 0: page = v; break;
            case 1: lmin = sval(v, n); break;
            case 2: lmax = n ? (lmin < 0 ? sval(v, n) : (int32_t)v) : 0; break;
            case 7: rsize = v; break;
            case 8: cur_id = (uint8_t)v; break;
            case 9: rcount = v; break;
            }
        } else if (type == 2) {                     /* 지역 */
            uint32_t u = n <= 2 ? (page << 16) | v : v;
            if (tag == 0 && nu < 32) usages[nu++] = u;
            else if (tag == 1) { umin = u; has_range = 1; }
            else if (tag == 2) { umax = u; has_range = 1; }
        } else if (type == 0) {                     /* 주 항목 */
            if (tag == 10) {                        /* Collection */
                if (nu && (usages[0] == 0x10004 || usages[0] == 0x10005) && pad_depth < 0) { is_pad = 1; pad_depth = depth; }
                depth++;
            } else if (tag == 12) {                 /* End Collection */
                depth--;
                if (depth == pad_depth) pad_depth = -2;   /* 첫 게임패드 모음만 */
            } else if (tag == 8) {                  /* Input */
                uint32_t *bp = &bitpos[cur_id];
                int in_pad = pad_depth >= 0 && (got_id < 0 || got_id == cur_id);
                for (uint32_t k = 0; k < rcount; k++) {
                    uint32_t u;
                    if (has_range) u = umin + k; else if (nu) u = usages[k < (uint32_t)nu ? k : (uint32_t)nu - 1]; else u = 0;
                    if (has_range && umax && u > umax) u = umax;
                    if (in_pad && !(v & 1)) {       /* 상수가 아닌 데이터 */
                        HidField f = { (uint16_t)*bp, (uint16_t)rsize, lmin, lmax, (uint8_t)(lmin < 0) };
                        int hit = 1;
                        if (u == 0x10030 && !p->x.size) p->x = f;
                        else if (u == 0x10031 && !p->y.size) p->y = f;
                        else if (u == 0x10039 && !p->hat.size) p->hat = f;
                        else if ((u >> 16) == 9 && (u & 0xFFFF) >= 1 && (u & 0xFFFF) <= 16) {
                            int bi = (int)(u & 0xFFFF) - 1;
                            p->btn[bi] = f; if (bi + 1 > p->nbtn) p->nbtn = bi + 1;
                        } else hit = 0;
                        if (hit && got_id < 0) got_id = cur_id;
                    }
                    *bp += rsize;
                }
            }
            if (tag == 8 || tag == 9 || tag == 11 || tag == 10) { nu = 0; has_range = 0; umin = umax = 0; }
        }
    }
    if (!is_pad || (!p->x.size && !p->hat.size)) { p->kind = 0; return 0; }
    p->report_id = got_id > 0 ? (uint8_t)got_id : 0;
    p->kind = 1;
    return 1;
}

void hidpad_xinput(HidPad *p)
{
    memset(p, 0, sizeof *p);
    p->kind = 2;
}

static uint32_t bits(const uint8_t *r, int len, uint32_t off, uint32_t size)
{
    uint32_t v = 0;
    for (uint32_t k = 0; k < size && k < 32; k++) {
        uint32_t bpos = off + k;
        if ((int)(bpos >> 3) >= len) break;
        if (r[bpos >> 3] & (1u << (bpos & 7))) v |= 1u << k;
    }
    return v;
}
static int32_t fval(const HidField *f, const uint8_t *r, int len)
{
    uint32_t v = bits(r, len, f->off, f->size);
    if (f->sign && f->size < 32 && (v & (1u << (f->size - 1)))) v |= ~0u << f->size;
    return (int32_t)v;
}
/* 축 → -1 / 0 / 1 (가운데에서 범위의 1/4 넘게 벗어나면) */
static int axis(const HidField *f, const uint8_t *r, int len)
{
    if (!f->size) return 0;
    int32_t v = fval(f, r, len), lo = f->min, hi = f->max;
    if (hi <= lo) { lo = 0; hi = (int32_t)((1u << (f->size < 31 ? f->size : 31)) - 1); }
    int32_t mid = lo + (hi - lo) / 2, dz = (hi - lo) / 4;
    if (v < mid - dz) return -1;
    if (v > mid + dz) return 1;
    return 0;
}

int hidpad_view(const HidPad *p, const uint8_t *r, int len, PadView *v)
{
    memset(v, 0, sizeof *v);
    v->hat = -1; v->kind = p->kind;
    v->dec = hidpad_decode(p, r, len);
    if (v->dec < 0) return -1;
    if (p->kind == 2) {                             /* Xbox 360: 왼쪽 스틱 6~9, 버튼 2~3 */
        int16_t lx = (int16_t)(r[6] | (r[7] << 8)), ly = (int16_t)(r[8] | (r[9] << 8));
        v->x = lx; v->y = -ly; v->xmin = v->ymin = -32768; v->xmax = v->ymax = 32767;
        uint16_t b = (uint16_t)(r[2] | (r[3] << 8));
        v->btn = b; v->nbtn = 16;
        int u = b & 1, d = (b >> 1) & 1, l = (b >> 2) & 1, rr = (b >> 3) & 1;
        if (u) v->hat = rr ? 1 : l ? 7 : 0;
        else if (d) v->hat = rr ? 3 : l ? 5 : 4;
        else if (rr) v->hat = 2;
        else if (l) v->hat = 6;
        return 0;
    }
    if (p->report_id) { r++; len--; }
    if (p->x.size) { v->x = fval(&p->x, r, len); v->xmin = p->x.min; v->xmax = p->x.max; }
    if (p->y.size) { v->y = fval(&p->y, r, len); v->ymin = p->y.min; v->ymax = p->y.max; }
    if (p->hat.size) {
        int32_t h = fval(&p->hat, r, len) - p->hat.min, n = p->hat.max - p->hat.min + 1;
        if (n == 8 && h >= 0 && h < 8) v->hat = h;
        else if (n == 4 && h >= 0 && h < 4) v->hat = h * 2;
    }
    for (int i = 0; i < p->nbtn && i < 16; i++)
        if (p->btn[i].size && bits(r, len, p->btn[i].off, 1)) v->btn |= 1u << i;
    v->nbtn = p->nbtn;
    return 0;
}

int hidpad_decode(const HidPad *p, const uint8_t *r, int len)
{
    int j = 0;
    if (p->kind == 2) {                             /* Xbox 360: 0 종류, 1 길이, 2~3 버튼, 6~9 왼쪽 스틱 */
        if (len < 10 || r[0] != 0x00) return -1;
        uint16_t b = (uint16_t)(r[2] | (r[3] << 8));
        if (b & 0x0001) j |= PAD_UP;
        if (b & 0x0002) j |= PAD_DOWN;
        if (b & 0x0004) j |= PAD_LEFT;
        if (b & 0x0008) j |= PAD_RIGHT;
        if (b & 0x0010) j |= PAD_START;
        if (b & 0x0020) j |= PAD_SELECT;
        if (b & 0x5000) j |= PAD_A;                 /* A, X */
        if (b & 0xA000) j |= PAD_B;                 /* B, Y */
        int16_t lx = (int16_t)(r[6] | (r[7] << 8)), ly = (int16_t)(r[8] | (r[9] << 8));
        if (lx < -12000) j |= PAD_LEFT;
        if (lx > 12000) j |= PAD_RIGHT;
        if (ly > 12000) j |= PAD_UP;                /* XInput은 위가 +값 */
        if (ly < -12000) j |= PAD_DOWN;
        return j;
    }
    if (p->kind != 1) return -1;
    if (p->report_id) {
        if (len < 1 || r[0] != p->report_id) return -1;
        r++; len--;
    }
    int x = axis(&p->x, r, len), y = axis(&p->y, r, len);
    if (x < 0) j |= PAD_LEFT;
    if (x > 0) j |= PAD_RIGHT;
    if (y < 0) j |= PAD_UP;
    if (y > 0) j |= PAD_DOWN;
    if (p->hat.size) {                              /* 0 위, 시계 방향 8방향, 범위 밖 = 가운데 */
        int32_t h = fval(&p->hat, r, len) - p->hat.min;
        int32_t n = p->hat.max - p->hat.min + 1;
        if (n == 8 && h >= 0 && h < 8) {
            static const uint8_t dir8[8] = { PAD_UP, PAD_UP | PAD_RIGHT, PAD_RIGHT, PAD_RIGHT | PAD_DOWN,
                                             PAD_DOWN, PAD_DOWN | PAD_LEFT, PAD_LEFT, PAD_LEFT | PAD_UP };
            j |= dir8[h];
        } else if (n == 4 && h >= 0 && h < 4) {
            static const uint8_t dir4[4] = { PAD_UP, PAD_RIGHT, PAD_DOWN, PAD_LEFT };
            j |= dir4[h];
        }
    }
    /* 버튼: 1·3 → A, 2·4 → B (어느 쪽 배치든 앞 버튼 넷이 모두 쓰이게), 9/7 Select, 10/8 Start */
    for (int i = 0; i < p->nbtn; i++) {
        if (!p->btn[i].size || !bits(r, len, p->btn[i].off, 1)) continue;
        if (i == 0 || i == 2) j |= PAD_A;
        else if (i == 1 || i == 3) j |= PAD_B;
        else if (i == 8) j |= PAD_SELECT;
        else if (i == 9) j |= PAD_START;
    }
    return j;
}
