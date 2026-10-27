/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* stdio.h - 본체용: emu2413의 디버그 출력은 쓰지 않음 */
#ifndef KSTDIO_H
#define KSTDIO_H
typedef struct KFILE FILE;
static inline int printf(const char *f, ...) { (void)f; return 0; }
#endif
