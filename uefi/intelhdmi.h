/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * intelhdmi.h - Intel Gen9(Skylake/Kaby Lake 계열) 내장 그래픽 HDMI 미러 출력 (UEFI 전용, 4.0.58)
 * 펌웨어가 HDMI를 GOP로 내주지 않는 PC(예: IdeaPad 310-15IKB, Intel HD 620)에서, 내장 패널(파이프 A)은 그대로 두고
 * 파이프 B -> 트랜스코더 B -> DDI B(HDMI)를 직접 켜서 같은 화면 버퍼(GGTT 0)를 HDMI 모니터에도 보여 준다.
 * 순서·값 출처: 리눅스 i915 (intel_display.c, intel_ddi.c, intel_dpll_mgr.c SKL, intel_ddi_buf_trans.c) — DisplaySelect v5에서 실기 검증.
 * Video 탭에서 Intel 그래픽일 때만 G(Enter)로 실행. AMD/NVIDIA PC는 건드리지 않는다.
 */
#ifndef INTELHDMI_H
#define INTELHDMI_H
#include <stdint.h>
/* stall_us: 마이크로초 지연, logw: 로그 파일 저장(경로, 내용, 길이) */
void intelhdmi_init(void (*stall_us)(uint32_t us), void (*logw)(const char *path, const uint8_t *d, uint32_t n), uint64_t gop_fb);
/* run=0: 읽기 점검만(쓰기 없음) / run=1: 꺼져 있으면 켜고, 이미 켰으면 되돌림. 반환 1=켜짐, 0=꺼짐/실패, -1=Intel 그래픽 아님 */
int intelhdmi_try(int run);
int intelhdmi_avail(void);        /* 화면 출력 목록에 올릴 수 있으면 장치 ID, 아니면 0 (읽기만) */
int intelhdmi_set(int mode);       /* 0 = 끄기, 1 = 복사(내부+HDMI), 2 = HDMI만(내장 패널 끔). 반환 = 지금 켜져 있으면 1 */
int intelhdmi_audio(int on);       /* 4.0.65: HDMI 소리 그래픽 쪽 켜기/끄기 (HDMI 미러가 켜진 뒤). 반환 = 켜져 있으면 1 */
int intelhdmi_audio_fresh(void);
void intelhdmi_wake(void);
int intelhdmi_lost(void);          /* 1 = HDMI 모니터가 뽑힘 (읽으면 지워짐) */
void intelhdmi_tick(uint32_t now_ms);   /* 자주 호출해도 됨 (내부에서 1초마다만 동작) */
#endif
