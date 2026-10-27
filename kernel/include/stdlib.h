/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* stdlib.h - 본체용: 고정 크기 메모리 풀 할당 (kernel/libm.c) */
#ifndef KSTDLIB_H
#define KSTDLIB_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void free(void *p);
#ifdef __cplusplus
}
#endif
#endif
