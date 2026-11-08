/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mmtext.c - X86 Multimedia 글자 그리기 (8.0.88). 규격: X86Multimedia_글자그리기_규격_V8.0.88 */
#include <string.h>
#include "mmtext.h"

extern UiFont font_t9;                    /* kernel/font_t9.c: Galmuri9 (BMP 전체) */
extern const uint16_t cp949_tab[126 * 190];   /* kernel/cp949_tab.c */
const UiFont *mmtext_font_big;

static const FontGlyph *glyph_of(const UiFont *f, uint32_t cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        uint32_t c = f->glyphs[m].cp;
        if (c == cp) return &f->glyphs[m];
        if (c < cp) lo = m + 1; else hi = m - 1;
    }
    return 0;
}

/* 한 글자 읽기. 돌려줌 = 쓴 바이트 수 (n 이 0 이면 0). *cp: 0xFFFFFFFF = 깨진/그릴 수 없는 글자 → ? */
#define BAD 0xFFFFFFFFu
static int next_char(int code, const uint8_t *s, int n, uint32_t *cp)
{
    if (n <= 0) return 0;
    uint8_t b = s[0];
    switch (code) {
    case 0: {                                                    /* UTF-8 */
        if (b < 0x80) { *cp = b; return 1; }
        int len = (b >= 0xC2 && b <= 0xDF) ? 2 : (b >= 0xE0 && b <= 0xEF) ? 3 : (b >= 0xF0 && b <= 0xF4) ? 4 : 0;
        if (!len || len > n) { *cp = BAD; return 1; }
        uint32_t v = b & (0xFF >> (len + 1));
        for (int i = 1; i < len; i++) { if ((s[i] & 0xC0) != 0x80) { *cp = BAD; return 1; } v = (v << 6) | (s[i] & 0x3F); }
        if ((len == 3 && v < 0x800) || (len == 4 && v < 0x10000) || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) { *cp = BAD; return len; }
        *cp = v > 0xFFFF ? BAD : v;                              /* BMP 밖은 글꼴에 없음 → ? */
        return len; }
    case 1: {                                                    /* CP949 */
        if (b < 0x80) { *cp = b; return 1; }
        if (b == 0x80 || b == 0xFF || n < 2) { *cp = BAD; return 1; }
        uint8_t t = s[1];
        if (t < 0x41 || t == 0xFF) { *cp = BAD; return 1; }
        uint16_t u = cp949_tab[(b - 0x81) * 190 + (t - 0x41)];
        *cp = u ? u : BAD; return 2; }
    default: {                                                   /* UTF-16 (2: LE, 3: BE) */
        if (n < 2) { *cp = BAD; return 1; }
        uint32_t u = code == 2 ? (uint32_t)(s[0] | s[1] << 8) : (uint32_t)(s[1] | s[0] << 8);
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (n >= 4) { uint32_t w = code == 2 ? (uint32_t)(s[2] | s[3] << 8) : (uint32_t)(s[3] | s[2] << 8); if (w >= 0xDC00 && w <= 0xDFFF) { *cp = BAD; return 4; } }
            *cp = BAD; return 2;
        }
        if (u >= 0xDC00 && u <= 0xDFFF) { *cp = BAD; return 2; }
        *cp = u; return 2; }
    }
}

static int px_on(const UiFont *f, const FontGlyph *g, int r, int c)
{
    return glyph_a(f, g, f->bits + g->off, r, c) >= 8;
}

int mmtext_draw(int code, int height, int maxw, const uint8_t *in, int n, uint8_t *out, int outmax)
{
    if (code < 0 || code > 3 || height < 8 || height > MMT_MAXH || n < 0 || n > MMT_MAXLEN) return 0;
    const UiFont *f = &font_t9;
    if (height >= 11 && mmtext_font_big) f = mmtext_font_big;
    if (f->height > MMT_MAXH) f = &font_t9;
    if (maxw <= 0 || maxw > MMT_MAXW) maxw = MMT_MAXW;
    int H = f->height, bpr = MMT_MAXW / 8;
    if (outmax < MMT_HDR + bpr * H) return 0;
    uint8_t *bm = out + MMT_HDR;
    memset(bm, 0, (size_t)bpr * H);
    const FontGlyph *q = glyph_of(f, '?');
    int x = 0, used = 0, drawn = 0, i = 0;
    while (i < n) {
        uint32_t cp; int k = next_char(code, in + i, n - i, &cp);
        if (k <= 0) break;
        if (cp == 0xFEFF || (cp < 0x20 && cp != BAD)) { i += k; used = i; continue; }       /* 글자 순서 표시·제어 글자는 건너뜀 */
        const FontGlyph *g = cp == BAD ? 0 : glyph_of(f, cp);
        if (!g) g = q;                                                                       /* 없는 글자·깨진 바이트 = ? */
        int adv = g ? g->adv : 6;
        if (x + adv > maxw) break;                                                           /* 폭 제한: 글자 단위로 자름 */
        if (g) {
            const int w = adv < g->bpr * (f->aa ? 2 : 8) ? adv : g->bpr * (f->aa ? 2 : 8);
            for (int r = 0; r < H; r++)
                for (int c = 0; c < w; c++)
                    if (px_on(f, g, r, c)) bm[r * bpr + ((x + c) >> 3)] |= (uint8_t)(0x80 >> ((x + c) & 7));
        }
        x += adv; i += k; used = i; drawn++;
    }
    int rowb = (x + 7) / 8;                                                                  /* 줄마다 ceil(폭/8) 바이트로 좁혀 앞으로 모음 */
    for (int r = 0; r < H; r++) memmove(bm + r * rowb, bm + r * bpr, (size_t)rowb);
    out[0] = (uint8_t)used; out[1] = (uint8_t)drawn; out[2] = (uint8_t)x; out[3] = (uint8_t)(x >> 8); out[4] = (uint8_t)H;
    return MMT_HDR + rowb * H;
}
