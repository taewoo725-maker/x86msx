/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* ivbhdmi.h - Ivy Bridge(HD 4000) + Panther Point: HDMI를 꽂고 부팅하면 내장 패널을 파이프 B로 같이 켜고, HDMI 소리 클럭을 맞춤 (UEFI, 4.0.110).
 * 근거: 기종 테스트방 DisplaySelect v38·v39 (ivb_dual.inc, ivb_audio.inc), Acer Aspire V5-171 실기 시험 성공. */
#ifndef IVBHDMI_H
#define IVBHDMI_H
#include <stdint.h>
void ivbhdmi_init(void (*stall_us)(uint32_t), void (*logw)(const char *, const uint8_t *, uint32_t));
void ivbhdmi_tick(uint32_t now_ms);   /* 자주 호출해도 됨: HDMI가 켜져 있고 내장 패널이 꺼져 있으면 한 번 켬 */
int ivbhdmi_hdmi_on(void);            /* Ivy Bridge이고 HDMI 포트가 켜져 있으면 1 */
int ivbhdmi_keep_mode(void);          /* Ivy Bridge이고 HDMI 포트가 켜져 있으면 1 — 이때는 GOP 화면 모드를 바꾸지 않음 (stall 설정 전에도 부름) */
void ivbhdmi_audio(void);             /* HDMI 소리를 고르기 직전: 포트 오디오 비트·AUD_CONFIG 클럭·GCP */
int ivbhdmi_avail(void);            /* 부팅 때 HDMI가 켜져 있던 Ivy Bridge면 장치 번호, 아니면 0 */
int ivbhdmi_set(int mode);            /* 0 = 내부(내장 패널 켬), 1 = 복사, 2 = HDMI만(내장 패널 끔). 성공 1 */
int ivbhdmi_lost(void);
int ivbhdmi_audio_fresh(void);
#endif
