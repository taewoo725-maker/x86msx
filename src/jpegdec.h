/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* jpegdec.h (8.0.120) - 베이스라인 JPEG 해독기 (AVI 의 MJPEG 용). 부동소수 없음, 플랫폼과 무관.
 * 결과는 I420 (Y, U, V 순서로 붙음; 폭·높이는 짝수로 올림, 색은 방송용 범위 Y16~235 / UV16~240).
 * 돌려줌: 0 = 성공, 음수 = 오류 (-1 깨짐, -2 지원 안 함(프로그레시브 등), -3 크기 초과) */
#ifndef JPEGDEC_H
#define JPEGDEC_H
#include <stdint.h>
#define JPG_MAXW 1280
#define JPG_MAXH 720
#define JPG_OUT_BYTES (JPG_MAXW * JPG_MAXH * 3 / 2 + 4096)
int jpg_decode(const uint8_t *d, uint32_t n, uint8_t *out, uint32_t cap, int *w, int *h);
#endif
