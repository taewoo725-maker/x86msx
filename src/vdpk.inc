/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vdpk.c - VDP 그리기 점 커널 (C판 하나만. 8.0.159: 비교용 어셈블러판 삭제) */
#include "vdpk.h"

static void c_put256(uint8_t *o, const uint8_t *l)
{
    for (int x = 0; x < 256; x++) { uint16_t pair = (uint16_t)(l[x] * 0x0101u); memcpy(o + 2 * x, &pair, 2); }   /* 점 하나를 두 번: 16비트 한 번에 */
}

static void c_nib16(uint8_t *o, const uint8_t *src, int n, const uint8_t *map16)
{
    for (int i = 0; i < n; i++) {
        uint8_t b = src[i];
        o[2 * i] = map16[b >> 4];
        o[2 * i + 1] = map16[b & 15];
    }
}

static void c_crumb4(uint8_t *o, const uint8_t *src, int n, const uint8_t *map4)
{
    for (int i = 0; i < n; i++) {
        uint8_t b = src[i];
        for (int k = 0; k < 4; k++) o[4 * i + k] = map4[(b >> (6 - 2 * k)) & 3];
    }
}

static uint64_t glyph_mask[256];                           /* 패턴 한 줄(8비트) → 켜진 점마다 0xFF 인 8바이트 (맨 앞 점 = 가장 낮은 바이트) */
static int glyph_mask_ready;
static void glyph_mask_init(void)
{
    for (int p = 0; p < 256; p++) {
        uint64_t m = 0;
        for (int k = 0; k < 8; k++) if (p & (0x80 >> k)) m |= (uint64_t)0xFF << (8 * k);
        glyph_mask[p] = m;
    }
    glyph_mask_ready = 1;
}

static void c_glyph8(uint8_t *o, const uint8_t *pat, const uint8_t *fg, const uint8_t *bg, int n)
{
    if (!glyph_mask_ready) glyph_mask_init();
    for (int i = 0; i < n; i++) {
        uint64_t m = glyph_mask[pat[i]], f = fg[i] * 0x0101010101010101ULL, b = bg[i] * 0x0101010101010101ULL;
        uint64_t v = (f & m) | (b & ~m);
        memcpy(o + 8 * i, &v, 8);
    }
}

const VdpKernels vdpk_c = { c_put256, c_nib16, c_crumb4, c_glyph8 };
const VdpKernels *vdpk = &vdpk_c;
