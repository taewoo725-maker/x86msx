/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* badges.h - 기종 세대 배지 (MSX / MSX2 / MSX2+ / turbo R), 2.8.1 */
#ifndef X86MSX_BADGES_H
#define X86MSX_BADGES_H
#define BADGE_GENS 4
#include <stdint.h>
typedef struct { int h, w; const uint8_t *rle; } Badge;   /* 2.17: 카드 색 위에 섞은 RGB, 줄마다 [개수 R G B] 묶음 */
#define BADGE_SIZES 1
extern const Badge badges[BADGE_SIZES][BADGE_GENS];           /* 2.17: 높이 48 하나 (화면에서 줄임) */
#endif
