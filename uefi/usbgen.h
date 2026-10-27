/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* usbgen.h - 3.5.5: USB 장치가 바뀐 횟수. main.c가 2초마다 한 번 USB 핸들 목록을 보고 바뀌었을 때만 올림.
 * 키보드·마우스·패드·MIDI가 따로 2초마다 핸들 목록을 뒤지던 것(같은 프레임에 몰려 주기적으로 끊김)을 대신함 */
#ifndef X86MSX_USBGEN_H
#define X86MSX_USBGEN_H
#include <stdint.h>
extern volatile uint32_t usb_gen;
#endif
