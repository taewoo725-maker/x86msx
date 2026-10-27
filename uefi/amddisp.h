/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* amddisp.h - AMD Renoir(DCN 2.1) HDMI 켜기/끄기 (UEFI 전용). 설명은 amddisp.c */
#ifndef AMDDISP_H
#define AMDDISP_H
#include <stdint.h>
void amddisp_setbios(const uint8_t *p, uint32_t n);    /* ACPI VFCT 에서 얻은 VBIOS 이미지 */
void amddisp_timing(void (*stall)(uint32_t));          /* UEFI Stall 로 TSC 속도 보정 (생략 가능) */
void amddisp_slow(int on);                             /* 1 = v53 과 같은 긴 대기 (화면이 안 나올 때 시험용) */
int amddisp_on(volatile uint32_t *mm);                 /* 0=켜짐, 1=내장 패널 상태 이상, 2=VBIOS 이상, 3=쓰는 중 버스 이상(재부팅 필요할 수 있음) */
void amddisp_off(volatile uint32_t *mm);               /* 원값 복원 */
int amddisp_panel(volatile uint32_t *mm, int off);       /* 내장 패널 백라이트 끄기(off=1)/되돌리기(off=0). 반환 0=처리됨, 그 외=못 끔(PWM 아님 등, 패널은 켜진 채) */
int amddisp_hist(char (*lines)[64], int max);          /* 기록 줄 */
#endif
