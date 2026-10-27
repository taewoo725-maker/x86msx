/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * amdedid.h - AMD DCN 2.1 HDMI 2b단계: DDC 비트뱅으로 EDID 블록0 읽기 (UEFI 전용)
 * 쓰는 곳: DC_GPIO_DDC2_{MASK,A,EN} 만 (저장 → 설정 → 반드시 복원). 선은 낮추거나 놓아줄 뿐 HIGH 구동 없음.
 * 레지스터 출처(MIT): torvalds/linux drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_offset.h/sh_mask.h
 */
#ifndef AMDEDID_H
#define AMDEDID_H
#include <stdint.h>
/* sense = HDMI(HPD1) 연결 감지 0/1. 값이 바뀔 때만 다시 읽는다. 진단 줄 수 반환 */
int amdedid_info(volatile uint32_t *mm, int sense, char (*lines)[64], int max);
#endif
