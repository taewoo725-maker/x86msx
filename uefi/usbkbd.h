/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* usbkbd.h - UEFI USB I/O 기반 HID 부트 키보드 (펌웨어 USB 스택 위) */
#ifndef MSX_USBKBD_H
#define MSX_USBKBD_H
#include "efi.h"
int  kbd_usb_init(EFI_BOOT_SERVICES *bs);
int  kbd_usb_count(void);
void kbd_usb_poll(void);
int  kbd_usb_info(char (*lines)[64], int max);
#endif
