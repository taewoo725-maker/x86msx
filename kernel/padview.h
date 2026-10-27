/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* padview.h - 진단 조이스틱 시험이 보는 보고 한 개 (3.8.1) */
#ifndef X86MSX_PADVIEW_H
#define X86MSX_PADVIEW_H
#include <stdint.h>
/* 3.8.1 진단 조이스틱 시험: 보고 하나를 있는 그대로 풀어 보임 (축 원값, 방향 스위치, 버튼 비트) */
typedef struct {
    int kind;                       /* 1 HID, 2 Xbox 360 */
    int x, y, xmin, xmax, ymin, ymax;   /* 축 원값과 범위 (없으면 min == max == 0) */
    int hat;                        /* 방향 스위치 0 위, 시계 방향 8방향 (2 오른쪽, 4 아래, 6 왼쪽), -1 가운데·없음 */
    uint32_t btn;                   /* 버튼 1~16 = 비트 0~15 */
    int nbtn;                       /* 쓰는 버튼 수 */
    int dec;                        /* hidpad_decode 결과 (PAD_*) */
} PadView;
#endif
