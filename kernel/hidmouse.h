/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* hidmouse.h - PC 마우스 보고 해석 (3.0): USB HID 보고 디스크립터, PS/2 묶음.
 * 플랫폼과 무관해 호스트에서 시험한다 (tests/mousetest.c). */
#ifndef X86MSX_HIDMOUSE_H
#define X86MSX_HIDMOUSE_H
#include <stdint.h>

typedef struct { uint16_t bit, size; int8_t sign; } HidField;     /* 보고 안 비트 위치·크기 (size 0 = 없음) */
typedef struct {
    uint8_t report_id;          /* 0 = 보고 번호 없음 */
    uint8_t nbuttons;
    uint16_t btn_bit;
    HidField x, y, wheel;
    int boot;                   /* 1 = 부트 프로토콜 (버튼, X, Y, [휠] 바이트) */
} HidMouse;

/* 보고 디스크립터에서 마우스(Generic Desktop / Mouse) 응용 모음을 찾아 필드 위치를 잡는다. 마우스면 0 */
int hidmouse_parse(HidMouse *m, const uint8_t *desc, int len);
void hidmouse_boot(HidMouse *m);                    /* 부트 프로토콜 고정 형식 */
/* 보고 하나 해석: 마우스 보고면 0과 이동량·휠·버튼 */
int hidmouse_decode(const HidMouse *m, const uint8_t *r, int len, int *dx, int *dy, int *dz, int *buttons);

/* PS/2: 바이트를 하나씩 넣는다. 묶음이 끝나면 1과 이동량(오른쪽·아래 +)·휠·버튼 */
typedef struct { uint8_t b[4]; int n, size; } Ps2Mouse;
int ps2mouse_byte(Ps2Mouse *p, uint8_t v, int *dx, int *dy, int *dz, int *buttons);
#endif
