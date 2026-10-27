/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * status.h - 상태 아이콘 선언
 *
 * 장치가 "어떤 상태 표시가 있고(아이콘 이름·글), 지금 켜졌나" 를 여기서 선언한다.
 * 프런트엔드는 이 표만 보고 그린다. 보이는 규칙(3초 뒤 사라짐·F6 숨김·메뉴를 열면 다 보임·롬 게임 중 숨김)은 fe.c 공용.
 * 미디어를 넣었나(디스크 이미지·하드 이미지)는 프런트엔드가 아는 것이라 fe.c 가 더함.
 * 새 장치 = 표에 한 줄 + msx_status() 에 상태 한 줄. 아이콘은 이름(icons.c 의 icon_name, USB Icons\이름.png).
 */
#ifndef STATUS_H
#define STATUS_H
#include "msx.h"

enum { MS_CAPS, MS_KANA, MS_FDD1, MS_FDD2, MS_CAS, MS_HD, MS_SLOT1, MS_SLOT2, MS_SLOT3, MS_SLOT4, MS_SLOT5, MS_CPU, MS_V9990, MS_NET, MS_N };

/* 상태 값 (예전 LED 값과 같음) */
enum { SS_OFF = 0, SS_ON = 2, SS_BUSY = 3, SS_NONE = 4 };   /* 꺼짐 / 켜짐·꽂힘(노랑) / 일하는 중(깜박임은 프런트엔드) / 이 기종에 없음(회색·안 보임) */

typedef struct {
    const char *key;        /* 진단·로그 이름 */
    const char *icon;       /* 아이콘 이름 (icon_name) */
    const char *icon_alt;   /* 다른 아이콘 (KANA → 일본 기종이 아니면 HANGUL, CPU → R800), 없으면 0 */
    const char *label;      /* 글 */
} MsxStatusDecl;
extern const MsxStatusDecl msx_status_decl[MS_N];

/* 본체(장치) 쪽 상태. alt[i] = 1 이면 icon_alt 를 씀 (CPU 가 R800 일 때). KANA/한글은 기종 이름으로 프런트엔드가 고름 */
void msx_status(MSX *m, uint8_t st[MS_N], uint8_t alt[MS_N]);
#endif
