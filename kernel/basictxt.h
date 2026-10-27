/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* basictxt.h - 3.2.7 MSX-BASIC 프로그램(메모리의 토큰)을 글(LIST와 같은 모양)로 바꾸기
 * peek(ctx, 주소)로 MSX 메모리를 읽는다(플랫폼과 무관). 줄 끝은 CR LF.
 * 돌려줌: 글 바이트 수(0 = 프로그램 없음), -1 = 메모리 모양이 이상함(BASIC이 아님), -2 = out이 모자람.
 * lines(NULL 가능)에 줄 수. */
#ifndef X86MSX_BASICTXT_H
#define X86MSX_BASICTXT_H
#include <stdint.h>
typedef uint8_t (*BasPeek)(void *ctx, uint16_t addr);
int basic_detok_at(BasPeek peek, void *ctx, uint16_t txttab, char *out, int max, int *lines);   /* 시작 주소를 직접 (파일 안 토큰 BAS) */
int basic_detok(BasPeek peek, void *ctx, char *out, int max, int *lines);
/* 한 줄(토큰, 0으로 끝)을 글로. 시험·내부용. 돌려줌: 쓴 바이트 수, -2 = 모자람 */
int basic_detok_line(BasPeek peek, void *ctx, uint16_t p, char *out, int max);
#endif
