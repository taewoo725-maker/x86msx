/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* usbmidi.h - 3.4.4 DEV-35: USB MIDI 입력·출력 (UEFI USB I/O 위, MSX-MIDI 받기·보내기) */
#ifndef MSX_USBMIDI_H
#define MSX_USBMIDI_H
#include "efi.h"
void midi_usb_init(EFI_BOOT_SERVICES *bs);
/* 받은 MIDI 바이트를 buf에 (최대 max). 장치가 없으면 -1. name에 장치 설명 */
int  midi_usb_read(uint8_t *buf, int max, char *name, int nmax);
/* 3.5.4: MIDI 바이트열을 장치의 OUT 끝점으로 (n = 0이면 확인만). 출력 장치가 없으면 -1 */
int  midi_usb_write(const uint8_t *buf, int n, char *name, int nmax);
int  midi_usb_info(char (*lines)[64], int max);
#endif
