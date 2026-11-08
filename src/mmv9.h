/* 만든이: KimTaeWoo
 * 도와준이: Lee.J.H
 * X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
 * mmv9.h (8.0.131) - X86 Video 카트리지가 V9990(Gfx9000)에 영상을 내보내는 부분. 실물 카트리지와 같은 규격:
 *   카트리지가 V9990 의 I/O 포트(60h~67h)로만 쓴다 (레지스터·VRAM 모두 포트 쓰기). 에뮬레이터는 같은 포트 쓰기를 그대로 흉내 낸다.
 *   OPEN 의 화면 번호 P4:  13 = B2 384x240 16비트 색(32768색, 기본·빠름)   14 = B4 768x240 8비트 색(256색 직접색, 흔들기 있음)
 *   순서: 소프트 리셋(67h=02h) → 해제(67h=01h, MCLK 켬) → 레지스터(64h 번호|80h, 63h 값): R6 모드, R7=0, R8=C0h(화면 켬·커서 끔), R13 색 방식,
 *         R0~R2 쓰기 주소 → 프레임마다 줄별로 쓰기 주소(R0~R2)를 정하고 60h 로 줄 바이트를 씀. 끝낼 때 67h=02h (V9990 화면 꺼짐)
 *   B2: R6=97h (이미지 폭 512, 점당 2바이트, 줄 간격 1024바이트)  B4: R6=AAh, R13=40h (이미지 폭 1024, 점당 1바이트 GGGRRRBB, 줄 간격 1024바이트) */
#ifndef MSX_MMV9_H
#define MSX_MMV9_H
#include "v9990.h"
#define MMV9_B2_16 13
#define MMV9_B4_256 14
int mmv9_is(int scr);                         /* 13·14 이면 1 */
void mmv9_start(V9990 *v, int scr);           /* V9990 을 해당 모드로 켜고 비움 */
#include "mmbar.h"
void mmv9_frame_bar(V9990 *v, int scr, const uint8_t *ov, const MmBarState *bar);   /* 8.0.151 bar != 0 이고 ov 가 있으면 맨 아래 20줄에 파란 표시줄 */
uint32_t mmv9_bar_color(int role);
void mmv9_frame(V9990 *v, int scr, const uint8_t *ov);   /* 마지막 해독 그림을 V9990 VRAM 에 (ov = 글자 줄 4096바이트, 0 이면 안 얹음) */
extern uint32_t mmv9_bg;                       /* 8.0.148: 영상 위·아래·좌우 띠와 글자 줄 바탕 색 0x00RRGGBB = MSX 테두리 색 (왼쪽·오른쪽 여백과 같은 색, 0 = 검정). mmvid 가 프레임마다 정함 */
void mmv9_diffuse8(const uint32_t *src, int w, int h, uint8_t *dst, int plain_top, int plain_bot);   /* RGB(0x00RRGGBB) -> GGGRRRBB 오차 확산 (시험용으로도 공개) */
void mmv9_stop(V9990 *v);                     /* V9990 을 리셋 상태로 (그 화면 꺼짐) */
#endif
