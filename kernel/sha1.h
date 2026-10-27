/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* sha1.h - SHA-1 */
#ifndef MSX_SHA1_H
#define MSX_SHA1_H
#include <stdint.h>
void sha1_hex(const uint8_t *data, uint32_t len, char out[41]);
#endif
