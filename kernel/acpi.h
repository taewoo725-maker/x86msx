/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* acpi.h - 3.3.2 PLAT-01 레거시 ACPI 전원 끄기 */
#ifndef X86MSX_ACPI_H
#define X86MSX_ACPI_H
int acpi_init(void);            /* 표 찾기 (0 = 끌 수 있음) */
void acpi_poweroff(void);       /* 꺼지면 돌아오지 않음 */
const char *acpi_status(void);  /* 진단 글 */
#endif
