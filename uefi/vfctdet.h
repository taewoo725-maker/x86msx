/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vfctdet.h - ACPI VFCT의 GPU VBIOS(ATOM) 유무를 읽기만 해서 진단 줄로 (UEFI 전용, 최초 1회 후 캐시) */
#ifndef VFCTDET_H
#define VFCTDET_H
#include "efi.h"
void vfct_setup(EFI_SYSTEM_TABLE *st);
int vfct_info(char (*lines)[64], int max);
#endif
