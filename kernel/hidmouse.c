/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* hidmouse.c - PC 마우스 보고 해석 (3.0). hidmouse.h 참고 */
#include "hidmouse.h"
#include <string.h>

static uint32_t uval(const uint8_t *d, int n) { uint32_t v = 0; for (int i = 0; i < n; i++) v |= (uint32_t)d[i] << (8 * i); return v; }
static int32_t sval(const uint8_t *d, int n)
{
    uint32_t v = uval(d, n);
    if (n == 1) return (int8_t)v;
    if (n == 2) return (int16_t)v;
    return (int32_t)v;
}

int hidmouse_parse(HidMouse *m, const uint8_t *desc, int len)
{
    memset(m, 0, sizeof *m);
    uint16_t page = 0, size = 0, count = 0, bitpos[256];
    int32_t lmin = 0;
    uint8_t rid = 0;
    uint32_t usage[16];
    int nu = 0, umin = -1, umax = -1, depth = 0, mouse_depth = -1, found = 0;
    struct { uint16_t page, size, count; int32_t lmin; uint8_t rid; } stack[4];
    int sp = 0;
    memset(bitpos, 0, sizeof bitpos);
    for (int i = 0; i < len; ) {
        uint8_t p = desc[i];
        if (p == 0xFE) { if (i + 2 >= len) break; i += 3 + desc[i + 1]; continue; }   /* 긴 항목: 건너뜀 */
        int n = p & 3; if (n == 3) n = 4;
        if (i + 1 + n > len) break;
        const uint8_t *d = desc + i + 1;
        int type = (p >> 2) & 3, tag = p >> 4;
        i += 1 + n;
        if (type == 1) {                                        /* 전역 */
            switch (tag) {
            case 0: page = (uint16_t)uval(d, n); break;
            case 1: lmin = sval(d, n); break;
            case 7: size = (uint16_t)uval(d, n); break;
            case 8: rid = (uint8_t)uval(d, n); break;
            case 9: count = (uint16_t)uval(d, n); break;
            case 10: if (sp < 4) { stack[sp].page = page; stack[sp].size = size; stack[sp].count = count; stack[sp].lmin = lmin; stack[sp].rid = rid; sp++; } break;
            case 11: if (sp > 0) { sp--; page = stack[sp].page; size = stack[sp].size; count = stack[sp].count; lmin = stack[sp].lmin; rid = stack[sp].rid; } break;
            }
            continue;
        }
        if (type == 2) {                                        /* 지역 */
            uint32_t u = uval(d, n);
            if (n < 4) u |= (uint32_t)page << 16;
            if (tag == 0 && nu < 16) usage[nu++] = u;
            else if (tag == 1) umin = (int)(u & 0xFFFF);
            else if (tag == 2) umax = (int)(u & 0xFFFF);
            continue;
        }
        if (type != 0) continue;
        switch (tag) {                                          /* 주 항목 */
        case 10: {                                              /* 모음 시작 */
            uint32_t u = nu ? usage[0] : 0;
            if (mouse_depth < 0 && uval(d, n) == 1 && (u == 0x00010002u || u == 0x00010001u)) mouse_depth = depth;
            depth++;
            break;
        }
        case 12:                                                /* 모음 끝 */
            if (depth > 0) depth--;
            if (mouse_depth >= 0 && depth == mouse_depth) { if (found) return 0; mouse_depth = -1; }
            break;
        case 8: {                                               /* 입력 */
            uint32_t flags = uval(d, n);
            if (mouse_depth >= 0 && !(flags & 1)) {             /* 상수(채움)가 아닌 것 */
                for (int k = 0; k < count; k++) {
                    uint32_t u;
                    if (k < nu) u = usage[k];
                    else if (umin >= 0) u = ((uint32_t)page << 16) | (uint32_t)(umin + k <= umax ? umin + k : umax);
                    else u = nu ? usage[nu - 1] : 0;
                    uint16_t bit = (uint16_t)(bitpos[rid] + k * size);
                    if ((u >> 16) == 9 && !m->nbuttons) { m->btn_bit = bit; m->nbuttons = (uint8_t)(count - k > 8 ? 8 : count - k); m->report_id = rid; break; }
                    if ((u >> 16) == 1 && (flags & 4)) {        /* 상대값만 (태블릿 같은 절대값은 뺌) */
                        HidField *f = (u & 0xFFFF) == 0x30 ? &m->x : (u & 0xFFFF) == 0x31 ? &m->y : (u & 0xFFFF) == 0x38 ? &m->wheel : 0;
                        if (f && !f->size) { f->bit = bit; f->size = size; f->sign = lmin < 0; m->report_id = rid; }
                    }
                }
                if (m->x.size && m->y.size) found = 1;
            }
            bitpos[rid] = (uint16_t)(bitpos[rid] + size * count);
            break;
        }
        case 9: case 11: break;                                 /* 출력·특징: 입력 보고 위치와 무관 */
        }
        nu = 0; umin = umax = -1;                               /* 주 항목마다 지역 값 지움 */
    }
    return found ? 0 : -1;
}

void hidmouse_boot(HidMouse *m)
{
    memset(m, 0, sizeof *m);
    m->boot = 1;
    m->nbuttons = 3; m->btn_bit = 0;
    m->x.bit = 8; m->x.size = 8; m->x.sign = 1;
    m->y.bit = 16; m->y.size = 8; m->y.sign = 1;
}

static int field(const uint8_t *r, int len, const HidField *f)
{
    if (!f->size || f->size > 32) return 0;
    uint32_t v = 0;
    for (int b = 0; b < f->size; b++) {
        int bit = f->bit + b;
        if (bit / 8 >= len) return 0;
        v |= (uint32_t)((r[bit / 8] >> (bit & 7)) & 1) << b;
    }
    if (f->sign && f->size < 32 && (v >> (f->size - 1)) & 1) v |= ~0u << f->size;
    return (int)v;
}

int hidmouse_decode(const HidMouse *m, const uint8_t *r, int len, int *dx, int *dy, int *dz, int *buttons)
{
    if (m->boot) {
        if (len < 3) return -1;
        *buttons = r[0] & 7; *dx = (int8_t)r[1]; *dy = (int8_t)r[2]; *dz = len >= 4 ? (int8_t)r[3] : 0;
        return 0;
    }
    if (m->report_id) { if (len < 1 || r[0] != m->report_id) return -1; r++; len--; }
    int b = 0;
    for (int i = 0; i < m->nbuttons; i++) {
        int bit = m->btn_bit + i;
        if (bit / 8 < len && ((r[bit / 8] >> (bit & 7)) & 1)) b |= 1 << i;
    }
    *buttons = b & 7;
    *dx = field(r, len, &m->x);
    *dy = field(r, len, &m->y);
    *dz = field(r, len, &m->wheel);
    return 0;
}

int ps2mouse_byte(Ps2Mouse *p, uint8_t v, int *dx, int *dy, int *dz, int *buttons)
{
    if (p->n == 0 && !(v & 0x08)) return 0;                     /* 첫 바이트는 비트 3이 늘 1: 아니면 맞춰 버림 */
    p->b[p->n++] = v;
    int size = p->size == 4 ? 4 : 3;
    if (p->n < size) return 0;
    p->n = 0;
    uint8_t s = p->b[0];
    if (s & 0xC0) return 0;                                     /* 넘침: 버림 */
    int x = p->b[1] - ((s & 0x10) ? 256 : 0), y = p->b[2] - ((s & 0x20) ? 256 : 0);
    *dx = x; *dy = -y;                                          /* PS/2는 위가 +Y */
    *dz = size == 4 ? -(int)(int8_t)((p->b[3] & 0x0F) | ((p->b[3] & 0x08) ? 0xF0 : 0)) : 0;
    *buttons = s & 7;
    return 1;
}
