/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mmtext.h - X86 Multimedia 글자 그리기 (영상 포트 30h TEXT, 8.0.88)
 * 글자 부호(UTF-8·CP949·UTF-16LE/BE)로 받은 문자열을 Galmuri 점글꼴로 1비트 비트맵으로 그려 돌려준다. */
#ifndef MSX_MMTEXT_H
#define MSX_MMTEXT_H
#include <stdint.h>
#include "../kernel/font.h"

#define MMT_MAXLEN   200      /* 입력 최대 바이트 */
#define MMT_MAXW     512      /* 비트맵 최대 폭(픽셀) */
#define MMT_MAXH     16
#define MMT_HDR      5        /* 결과 앞머리: 쓴 바이트 수, 그린 글자 수, 폭(2), 높이 */
#define MMT_OUTMAX   (MMT_HDR + (MMT_MAXW / 8) * MMT_MAXH)

/* 높이 11~16 은 이 글꼴로 (없으면 Galmuri9 로). 플랫폼이 내장 Galmuri11 (줄 높이 16) 을 넣어 줌 */
extern const UiFont *mmtext_font_big;

/* 돌려줌: 결과 바이트 수 (0 = 오류: 높이 8~16 밖, 부호 0~3 밖, 길이 200 초과) */
int mmtext_draw(int code, int height, int maxw, const uint8_t *in, int n, uint8_t *out, int outmax);
#endif
