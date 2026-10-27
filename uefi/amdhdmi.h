/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * amdhdmi.h - AMD Renoir(DCN 2.1, PCI 1002:1636) HDMI 영상 + 소리(그래픽 쪽) 켜기 (UEFI 전용, 4.0.115)
 * 내장 패널(eDP, 파이프 0)은 그대로 두고, 파이프 1 -> DIG1 -> UNIPHYB(HDMI)를 켜서 내장 패널과 같은 화면을 HDMI에도 보여 준다.
 * 순서·값은 DisplaySelect 1번(V14-ARE) v53 (실기: LG TV 1920x1080@60에서 영상 + 소리 8초 확인) 그대로. 1920x1080 내장 패널 + 1080p60 HDMI 한 조합만 확인됨.
 * HDMI는 부팅 전에 꽂아 둔 것만 지원(핫플러그 켜기는 아직 없음). 소리의 HDA 쪽(코덱)은 kernel/hda.c.
 */
#ifndef AMDHDMI_H
#define AMDHDMI_H
#include <stdint.h>
#include "efi.h"
void amdhdmi_init(EFI_SYSTEM_TABLE *st, void (*stall_us)(uint32_t us), void (*logw)(const char *path, const uint8_t *d, uint32_t n));
int amdhdmi_avail(void);            /* 화면 출력 목록에 올릴 수 있으면 장치 ID(0x1636), 아니면 0 (읽기만: GPU 찾기 + HDMI 연결 감지) */
int amdhdmi_set(int mode);          /* 0 = 끄기(원래대로), 1 = 복사(내부+HDMI). 반환 = 지금 켜져 있으면 1. 켜는 데 6~8초 걸림 */
int amdhdmi_audio_fresh(void);      /* 켠 지 3초 뒤 1회 1 (그래픽 쪽 소리 설정은 켜는 순서에 들어 있음. HDA 코덱을 열어도 된다는 신호) */
int amdhdmi_lost(void);             /* 1 = HDMI 모니터가 뽑힘 (한 번 읽으면 지워짐) */
void amdhdmi_tick(uint32_t now_ms);
extern int g_amd_quiet;             /* 1이면 단계 번호 화면 표시를 하지 않음 */
#endif
