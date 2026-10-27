/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* font.h - 화면 글꼴 (Galmuri, SIL OFL). UTF-8 문자열을 그린다. */
#ifndef MSX_FONT_H
#define MSX_FONT_H
#include <stdint.h>

typedef struct { uint32_t cp; uint32_t off; uint8_t adv, bpr; } FontGlyph;
typedef struct {
    int height, ascent, count;
    const FontGlyph *glyphs;
    const uint8_t *bits;
    int aa;                        /* 1이면 4비트 알파(부드러운 글꼴), 0이면 1비트 점글꼴 */
} UiFont;

/* 글자 g의 r줄 c칸 진하기 0..15 (1비트 글꼴은 0 또는 15) */
static inline int glyph_a(const UiFont *f, const FontGlyph *g, const uint8_t *b, int r, int c)
{
    if (f->aa) { uint8_t v = b[r * g->bpr + (c >> 1)]; return (c & 1) ? (v & 15) : (v >> 4); }
    return (b[r * g->bpr + (c >> 3)] & (0x80 >> (c & 7))) ? 15 : 0;
}

extern UiFont font_small;     /* 내장 Galmuri11 (파일 글꼴이 있으면 그것으로 바뀜): 줄 높이 16 */
extern UiFont font_large;     /* 내장 Galmuri14: 줄 높이 21 */
#define font_mid font_large        /* 칸 안 글씨 = 큰 글꼴 (Noto Sans 14px) */
extern UiFont font_btn;       /* 13 px 단추 글씨 (줄 높이 16) */
extern UiFont font_head;      /* 16 px 굵게: 소제목 (줄 높이 21) */
extern UiFont font_title;     /* 20 px 굵게: 창 제목 (줄 높이 21) */
extern UiFont font_tiny;      /* 칸 안 값 글씨 (Noto 11px, 줄 높이 16). 내장은 작은 글꼴과 같음 */
extern UiFont font_bold;      /* 굵은 글꼴: 이름 (MIDI 창 시안). 내장은 큰 글꼴과 같음 */

/* UTF-8 한 글자 읽기. *s를 다음 글자로 옮긴다 */
uint32_t utf8_next(const char **s);
const FontGlyph *font_glyph(const UiFont *f, uint32_t cp);
#endif
