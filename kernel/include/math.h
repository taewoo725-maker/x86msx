/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* math.h - 본체용 최소 수학 함수 (kernel/libm.c, emu2413의 샘플링 변환기용) */
#ifndef KMATH_H
#define KMATH_H
#ifdef __cplusplus
extern "C" {
#endif
double sin(double x);
double cos(double x);
double floor(double x);
double round(double x);
#ifdef __cplusplus
}
#endif
#endif
