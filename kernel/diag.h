/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * diag.h - 진단 정보 분류·판정 (플랫폼과 상관없는 순수 코드, 호스트 시험 tests/diagtest.c)
 *
 * 플랫폼(UEFI·레거시)이 주는 진단 줄(P.info)을 영역으로 나누고, 영역마다 상태(정상·주의·실패)와 이유 코드를 정한다.
 * 진단 화면의 "요약" 탭과 LOG\DIAG.TXT(공용 로그 규약: AREA.item key=val res=ok|fail|skip why=…)가 같은 결과를 쓴다.
 * 규칙을 바꾸거나 새 장치 줄을 넣을 때: diag.c 의 표 한 줄 + tests/diagtest.c 의 실제 LOG 예 한 줄.
 */
#ifndef X86MSX_DIAG_H
#define X86MSX_DIAG_H
#include <stdint.h>

typedef enum { DA_SYS, DA_VID, DA_SND, DA_KBD, DA_MOUSE, DA_TP, DA_PAD, DA_USB, DA_NET, DA_PERF, DA_N } DiagArea;
typedef enum { DS_INFO, DS_OK, DS_WARN, DS_FAIL, DS_NA } DiagState;   /* DS_NA = 진단 안 됨 (자료가 없어 판정 못 함) */

typedef struct {
    DiagState st;
    char why[32];                       /* 짧은 이유 코드 (로그 규약 why=) — 정상이면 빈 칸 */
    char text[96];                      /* 한 줄 설명 (영문, 화면·로그 공용) */
} DiagSum;

typedef struct {                        /* 판정에 쓰는 프런트엔드 상태 */
    const char *snd_status, *wc_status, *vs_status, *usb_status, *machine;
    int speed, busy, gap_ms;
} DiagCtx;

extern const char *const diag_area_key[DA_N];        /* 로그 영역 이름 SYS VID SND … */

/* 5.2.26 시험 PC 표: 실기에서 확인한 PC 와 영역마다 "예상" — 예상과 다르면 판정에 표시 (X86MSX_시험PC목록·기종별_반영현황 문서 기준) */
typedef enum { EX_UNKNOWN, EX_OK, EX_UNSUP, EX_NONE } DiagExpect;   /* 모름(아직 시험 안 함) · 동작해야 함 · 지원 안 함(알고 있음) · 장치 없음 */
typedef struct {
    int pc;                             /* 시험 PC 번호 (0 = 아직 번호 없음) */
    const char *name;
    const char *gpu;                    /* 영상칩 PCI "8086:7D55" (자동 판별), 없으면 0 */
    const char *smbios;                 /* SMBIOS system 줄에 들어 있는 글 (자동 판별), 없으면 0 */
    const char *boot;                   /* "uefi" "legacy" "both" */
    const char *codec;                  /* 소리 코덱 "10EC:0298" (다르면 SND 주의 codec_mismatch), 모르면 0 */
    uint8_t ex[DA_N];                   /* 영역마다 DiagExpect */
    const char *note;                   /* 알려진 것 (로그에 그대로) */
} DiagPc;
extern const DiagPc diag_pcs[];
extern const int diag_npcs;
/* 사용자가 더한 시험 PC (USB 0:\TESTPC.TXT, 한 줄에 PC 하나). diag_pc() 번호는 기본 표 다음에 이어짐 */
#define DIAG_UPC_MAX 32
#define DIAG_TESTPC_PATH "0:\\TESTPC.TXT"
int diag_pc_count(void);                                /* 기본 표 + 사용자 PC */
const DiagPc *diag_pc(int i);                           /* 0 ~ count-1, 없으면 0 */
int diag_pc_is_user(int i);
/* TESTPC.TXT 읽기 (줄: pc=7 name="…" smbios="…" gpu=8086:7D55 codec=10EC:0298 boot=uefi ex=OOOOOXUOXO note="…"). 돌려줌: 사용자 PC 수 */
int diag_pc_user_load(const char *txt, int len);
/* 지금 PC 를 진단 줄·판정으로 사용자 PC 에 더함 (같은 SMBIOS 의 사용자 PC 는 새로 씀). 돌려줌: 전체 번호, 실패 -1 */
int diag_pc_user_add(char (*lines)[64], int n, const DiagSum s[DA_N], const char *ver);
/* TESTPC.TXT 본문. 돌려줌: 길이 */
int diag_pc_user_save(char *b, int max);
/* 진단 줄로 자동 판별 (사용자 PC 먼저, 못 찾으면 -1). 돌려줌: diag_pc() 번호 */
int diag_pc_auto(char (*lines)[64], int n);
/* 고른 PC 의 예상을 판정에 반영: 지원 안 함으로 알려진 주의·실패 → 정보(known_unsupported), 동작해야 하는데 실패 → 실패(why 앞에 "regress_") */
void diag_expect(const DiagPc *pc, char (*lines)[64], int n, DiagSum s[DA_N]);

/* 진단 줄 하나의 영역 (모르면 DA_SYS) */
DiagArea diag_area_of(const char *line);
/* 영역마다 상태 판정. lines = 플랫폼 진단 줄 n개 */
void diag_judge(const DiagCtx *c, char (*lines)[64], int n, DiagSum out[DA_N]);
/* LOG\DIAG.TXT 본문 (규약 머리·요약·영역별 줄). 돌려줌: 길이 */
int diag_format(char *b, int max, const char *ver, const DiagCtx *c, char (*lines)[64], int n, const DiagSum sum[DA_N], const DiagPc *pc, int pc_auto);
#endif
