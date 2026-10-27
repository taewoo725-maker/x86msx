/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* usbpad.h - USB 게임패드 (2.13, UEFI USB I/O 위) */
#ifndef MSX_USBPAD_H
#define MSX_USBPAD_H
#include "efi.h"
int  pad_usb_init(EFI_BOOT_SERVICES *bs);
int  pad_usb_count(void);
void pad_usb_poll(void);
/* idx번째 패드의 PAD_* 비트 (없으면 -1). name이 있으면 이름 */
int  pad_usb_read(int idx, char *name, int max);
/* 3.8.1: 진단 조이스틱 시험 — 풀어 낸 마지막 보고. 패드 없음 -1, 보고 없음 0, 있음 1 */
#include "../kernel/padview.h"
int  pad_usb_view(int idx, PadView *v, char *name, int max);
int  pad_usb_info(char (*lines)[64], int max);
#endif
