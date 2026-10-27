/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* png.h - 아이콘용 작은 PNG 해독 (16 x 16 모양으로) */
#ifndef MSX_PNG_H
#define MSX_PNG_H
#include <stdint.h>
/* PNG를 16x16 모양으로: 투명도가 있으면 불투명한 점, 없으면 어두운 점(밝기 < 128)이 '#'.
 * 크기가 다르면 16x16으로 줄여 씀. 성공 0 */
int png_icon16(const uint8_t *png, uint32_t len, char out[16][17]);
/* 일반 해독 (아이콘·배지): 성공 0, 그 뒤 png_rgba로 점마다 읽음. 최대 512 x 128 */
int png_load(const uint8_t *png, uint32_t len, uint32_t *w, uint32_t *h);
int png_load_big(const uint8_t *png, uint32_t len, uint32_t *w, uint32_t *h, uint8_t *mem);   /* 4.0.31: 미리보기 (최대 1280 x 800). mem = PNG_BIG_MEM 바이트 작업 자리 */
#define PNG_BIG_MEM (800u * (1280u * 3 + 1) + (2u << 20))
void png_rgba(uint32_t x, uint32_t y, int rgba[4]);
int png_has_alpha(void);
#endif
