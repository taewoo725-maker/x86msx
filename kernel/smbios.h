/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* smbios.h - 5.2.26: SMBIOS 에서 PC 이름(제조사·모델)·BIOS·보드 읽기 (읽기만) — 진단·LOG 머리에서 어느 PC 인지 알게 */
#ifndef X86MSX_SMBIOS_H
#define X86MSX_SMBIOS_H
#include <stdint.h>
/* ep = "_SM3_" 또는 "_SM_" 입구. 돌려줌: 만든 줄 수 ("SMBIOS system …", "SMBIOS bios …", "SMBIOS board …") */
int smbios_lines(const uint8_t *ep, char (*lines)[64], int max);
/* 레거시: 0xF0000~0xFFFFF 에서 입구 찾기 (없으면 0) */
const uint8_t *smbios_find_legacy(void);
#endif
