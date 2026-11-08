/* 만든이: KimTaeWoo
 * 도와준이: Lee.J.H
 * X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
 * v9_conv.h (8.0.136) - src/v9_conv.asm (SSE2) 의 구조체와 함수. 구조체 배치는 어셈 쪽 주석과 같아야 함. 어셈은 ini V9Asm=1(기본)일 때만 부른다. */
#ifndef MSX_V9_CONV_H
#define MSX_V9_CONV_H
#include <stdint.h>
typedef struct { const uint8_t *y, *u, *v; uint32_t *out; int n, hd; } V9Cv;               /* 오프셋 0 8 16 24 32 36 */
typedef struct { const uint32_t *rgb; uint8_t *out; int n; } V9P15;                        /* 0 8 16 */
void v9_yuv2rgb(V9Cv *a);        /* Y·U·V 바이트 n개 → 0x00RRGGBB n개 (n 은 8의 배수) */
void v9_pack15(V9P15 *a);        /* 0x00RRGGBB n개 → 16비트 색 n개 (2n 바이트) */
#endif
