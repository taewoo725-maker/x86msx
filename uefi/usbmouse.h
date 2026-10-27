/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* usbmouse.h - USB 마우스 (3.0, UEFI USB I/O 위) */
#ifndef MSX_USBMOUSE_H
#define MSX_USBMOUSE_H
#include "efi.h"
int  mouse_usb_init(EFI_BOOT_SERVICES *bs);
int  mouse_usb_count(void);
void mouse_usb_poll(void);
/* 지난 호출 뒤 이동량·휠, 지금 버튼. 마우스 개수 반환 */
int  mouse_usb_read(int *dx, int *dy, int *dz, int *buttons, char *name, int max);
int  mouse_usb_dev(int n, char *name, int max, int *on);
void mouse_usb_set(int n, int on);
int  mouse_usb_info(char (*lines)[64], int max);
int  mouse_usb_driver(int mode);   /* <0 묻기, 0 펌웨어 드라이버 먼저, 1 직접(비동기), 2 직접(동기 읽기). 지금 값 */
#endif
