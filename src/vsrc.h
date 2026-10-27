/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * vsrc.h - 화면 소스 표 — 화면 출력 층(합성기)이 어느 영상 장치를 보일지 정함.
 * 영상 장치는 이 표에 한 줄: 이름·ini 값·꽂혀 있나·지금 화면을 내보내나·화면 비율·자동일 때 우선순위.
 * 프런트엔드는 고른 소스의 그리기 함수만 부름 (fe.c vsrc_draw 표). 새 영상 장치 = 이 표 한 줄 + 그리기 함수 하나
 */
#ifndef VSRC_H
#define VSRC_H
#include "msx.h"

typedef enum { VSRC_VDP, VSRC_V9990, VSRC_N } VSrcId;

typedef struct {
    const char *key;                    /* ini 값 (VideoSource=) */
    const char *name;                   /* 화면에 보일 이름 */
    int (*present)(const MSX *m);       /* 장치가 있나 (꽂혀 있나) */
    int (*active)(const MSX *m);        /* 지금 화면을 내보내나 (자동일 때 이것으로 고름) */
    uint8_t aspect43;                   /* 1 = 4:3 화면 (MSX 화면 비율이 아님) */
    uint8_t prio;                       /* 자동: 내보내는 것 중 큰 것 */
} VSrcInfo;

extern const VSrcInfo msx_vsrc[VSRC_N];

/* 보일 소스: pref < 0 = 자동, 아니면 그 소스 (없으면 자동) */
int msx_vsrc_pick(const MSX *m, int pref);
#endif
