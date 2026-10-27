/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * media.h - 미디어 종류 선언 — 넣을 수 있는 미디어(롬·디스크·테이프·하드·BASIC 글)마다
 * 파일 창에 보일 확장자, 넣을 때만 받는 확장자, zip 안에서 꺼내 쓸 수 있나를 표 한 줄로.
 * 프런트엔드의 파일 창(보일 파일)·넣기(맞지 않는 종류 알림)는 이 표만 본다. 새 미디어 종류 = 표 한 줄.
 */
#ifndef MEDIA_H
#define MEDIA_H

typedef enum { MK_ROM, MK_DISK, MK_HD, MK_TAPE, MK_BASIC, MK_N } MediaKind;

typedef struct {
    const char *key;        /* 짧은 이름 (로그·ini) */
    const char *exts;       /* 파일 창에 보이고 넣을 수 있는 확장자, 빈칸으로 나눔 (소문자) */
    const char *extra;      /* 파일 창에는 안 보이지만 넣을 수는 있는 확장자 (롬 .bin) */
    unsigned char zip;      /* zip 을 보이고 받음 (안의 파일을 꺼내 씀) */
} MediaKindInfo;

extern const MediaKindInfo msx_media_kind[MK_N];

/* name 의 확장자가 빈칸으로 나눈 목록 list 에 있나 (대소문자 무시) */
int media_ext_in(const char *list, const char *name);
/* 파일 창에 보일 파일인가 (no_zip=1: BASIC 저장 창 — zip 을 안 보임, 다른 종류에는 영향 없음) */
int media_shows(int kind, const char *name, int no_zip);
/* 이 미디어 자리에 넣을 수 있나 (zip 은 hd 가 아니면 받음 — 안을 보고 다시 고름) */
int media_accepts(int kind, const char *name);
/* 미디어 자리 — 기종(장치)에 실제로 있는 자리. 없는 자리는 메뉴에서 흐리게 (마스터님 결정) */
#include "msx.h"
typedef struct {
    unsigned char cart[MSX_CART_SLOTS];   /* 카트 자리 연결됨 (기종의 슬롯 A~E) */
    unsigned char disk;                   /* 디스크 드라이브 수 0~2 (FDC 와 기종 파일의 드라이브 수) */
    unsigned char tape;                   /* 카세트 단자 (turbo R 에는 없음) */
} MsxMediaSlots;
void msx_media_slots(const MSX *m, MsxMediaSlots *s);
/* 확장자로 본 미디어 종류 (롬 → 디스크 → 하드 → 테이프 차례), 모르면 -1. 다른 창에 잘못 넣었을 때 알림용 */
int media_kind_guess(const char *name);
#endif
