/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* arcx.h - 8.0.44 .ARJ·.PMA 묶음 파일 읽기. zipx.c 가 맨 앞 글자로 알아보고 같은 함수 모양으로 넘겨 준다 (zip 쪽 코드는 그대로).
 *  ARJ: 저장(0)·방법 1~3·4(빠른). 암호·여러 조각은 ZIPX_METHOD.  PMA(PMarc, LHA 머리글): -pm0-(저장)·-pm1-·-pm2-. */
#ifndef X86MSX_ARCX_H
#define X86MSX_ARCX_H
#include "zipx.h"
int arc_kind(const uint8_t *d, uint32_t len);          /* 0 = ARJ·PMA 아님, 1 = ARJ, 2 = PMA */
int arc_extract(const uint8_t *a, uint32_t alen, const char *exts, uint8_t *out, uint32_t max, uint32_t *size, char *name, int namemax);
int arc_extract_nth(const uint8_t *a, uint32_t alen, const char *exts, int nth, uint8_t *out, uint32_t max, uint32_t *size, char *name, int namemax, int *total);
int arc_names_ex(const uint8_t *a, uint32_t alen, const char *exts, char (*names)[48], uint32_t *usize, uint32_t *dos, int max);
int arc_peek(const uint8_t *a, uint32_t alen, const char *exts, char *name, int namemax, uint32_t *usize_out);
int arc_list(const uint8_t *a, uint32_t alen, const char *dir, ZipListCb cb, void *ctx);
int arc_read(const uint8_t *a, uint32_t alen, const char *path, uint8_t *out, uint32_t max, uint32_t *size);
#endif
