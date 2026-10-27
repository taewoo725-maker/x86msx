/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* icons.h - 상태 표시 아이콘 */
#ifndef MSX_ICONS_H
#define MSX_ICONS_H
#include <stdint.h>
enum { ICON_CAPS, ICON_KANA, ICON_FDD, ICON_CAS, ICON_HD, ICON_CART, ICON_SWITCH, ICON_MIXER, ICON_KEYBOARD, ICON_GEAR,
       ICON_PSG, ICON_SCC, ICON_PCM, ICON_MM, ICON_MA, ICON_MS, ICON_OPP, ICON_MIDI, ICON_HANGUL,
       ICON_FOLDER, ICON_FILE, ICON_POINTER,
       ICON_RESET, ICON_POWER, ICON_SPEED, ICON_MONITOR, ICON_JOY, ICON_SAVE, ICON_LOAD,
       ICON_CAMERA, ICON_MOUSE, ICON_PAUSE, ICON_MENU, ICON_SCAN, ICON_MEDIA, ICON_PSG2,
       ICON_SOFTMIDI, ICON_USBMIDI, ICON_ETC, ICON_ZIP, ICON_HDD, ICON_PRINTER, ICON_Z80, ICON_R800, ICON_TEXT, ICON_STATE, ICON_SOUND, ICON_DISKIMG, ICON_OTHER, ICON_DSK360, ICON_DSK144, ICON_DSK720, ICON_ARROWR, ICON_ARROWL, ICON_PICTURE, ICON_MOVIE, ICON_NET, ICON_V9990, ICON_COUNT };   /* 3.7.0: SOFT MIDI, USB MIDI, 기타(카트리지 음원 = 이름 첫 글자를 얹음) */   /* 3.5.0: RESET~MEDIA 도구 막대·미디어 판 */   /* 3.0.1: 파일 선택의 폴더·파일, 마우스 포인터 */
/* 아이콘 이름 = USB의 Icons\이름.png (바꿔 쓸 수 있음) */
extern const char *const icon_name[ICON_COUNT];
/* 2.17: 부드러운 아이콘 48x48, [0] 바탕 [1] 강조, 4비트 알파 두 점씩 (tools/mkicons_hd.py) */
extern const uint8_t icon_hd[ICON_COUNT][2][48 * 48 / 2];
#endif
