/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* hidpad.h - USB 게임패드 보고 해석 (2.13). 플랫폼과 무관, 호스트에서 시험 (tests/hidpadtest.c)
 *
 * HID 보고 디스크립터에서 X·Y 축, 방향 스위치(hat), 버튼 1~16의 자리를 찾아 두고,
 * 보고가 올 때마다 MSX 조이스틱 비트로 바꾼다. Xbox 360(XInput) 형식은 고정 자리. */
#ifndef X86MSX_HIDPAD_H
#define X86MSX_HIDPAD_H
#include <stdint.h>

typedef struct {
    uint16_t off, size;             /* 보고 안의 비트 위치·크기 (size 0 = 없음) */
    int32_t min, max;               /* 논리 최솟값·최댓값 */
    uint8_t sign;                   /* 최솟값이 음수면 부호 있는 값 */
} HidField;

typedef struct {
    int kind;                       /* 0 해석 못 함, 1 HID 게임패드·조이스틱, 2 Xbox 360 */
    uint8_t report_id;              /* 0 = 보고 번호 없음 */
    HidField x, y, hat;
    HidField btn[16];
    int nbtn;
} HidPad;

/* MSX 조이스틱 비트 (msx.h joy[]와 같음) + 추가 버튼 */
#define PAD_UP     0x01
#define PAD_DOWN   0x02
#define PAD_LEFT   0x04
#define PAD_RIGHT  0x08
#define PAD_A      0x10
#define PAD_B      0x20
#define PAD_START  0x40
#define PAD_SELECT 0x80

/* 디스크립터 해석. 게임패드(사용 페이지 1, 사용 4 조이스틱 / 5 게임패드)면 1 */
int hidpad_parse(const uint8_t *desc, int len, HidPad *p);
/* Xbox 360 유선 패드 (클래스 FF/5D/01) */
void hidpad_xinput(HidPad *p);
/* 보고 하나 → PAD_* 비트. 이 패드의 보고가 아니면 -1 */
int hidpad_decode(const HidPad *p, const uint8_t *rep, int len);

#include "padview.h"

int hidpad_view(const HidPad *p, const uint8_t *rep, int len, PadView *v);
#endif
