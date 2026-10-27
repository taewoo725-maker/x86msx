/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * hda.h - Intel High Definition Audio 출력 (설계서 §4.7 2단계)
 *
 * PCI 클래스 04:03 컨트롤러 → 코덱의 스피커/헤드폰 핀에서 DAC까지 경로를 찾아 켜고
 * 첫 출력 스트림을 48 kHz 16비트 스테레오 링 버퍼로 재생한다.
 * DMA 구조체와 버퍼는 hda_init에 넘긴 영역(레거시 MEM_HDA_DMA 0x02000000, UEFI는 할당)의 4 MiB 페이지에 두며, 이 페이지는 캐시 안 됨(UC)으로 매핑한다.
 */
#ifndef HDA_H
#define HDA_H
#include <stdint.h>

#define HDA_DMA_MIN  0x00100000u

/* 초기화. 성공 0. status에 표시용 짧은 문자열(최대 20자)
 * dma: 64 KiB 이상, 4 GiB 아래, 128바이트 정렬된 DMA 영역 (가상 = 물리) */
int hda_init(char *status, int len, uintptr_t dma);
void hda_set_dma_dev(int64_t off);
extern void (*hda_trace)(const char *);          /* 진단 추적 */              /* 장치 주소 - 물리 주소 (UEFI VT-d Map 결과, 보통 0) */
/* 출력 장치 다시 고르기: 0 자동, 1 스피커·라인 출력(아날로그), 2 HDMI·DisplayPort(디지털). 성공 0 */
int hda_select(int pref, char *status, int len);
const char *hda_dbg_text(void);   /* 4.0.84: 마지막 소리 선택 과정 기록 */
/* 재생이 멈춰 자동으로 다시 초기화한 횟수 */
int hda_recoveries(void);

/* 모노 샘플(PSG 출력, 0 이상 값)을 스테레오로 바꿔 재생 버퍼에 넣는다.
 * volume 0~100, mute면 무음 */
void hda_write(const int16_t *mono, int n, int volume, int mute);
void hda_write2(const int16_t *left, const int16_t *right, int n, int volume, int mute);   /* 3.3.3 스테레오 (right NULL = 모노) */

int hda_ok(void);
int hda_hdmi_present(void);             /* 4.0.102: 그래픽 코덱의 모니터 감지 (-1 모름) */
int hda_headphone(void);                /* 4.0.34: 1 꽂힘, 0 빠짐, -1 모름 */
void hda_silence(void);                 /* 3.8.1: 재생 버퍼를 비우고 스트림 멈춤 (재부팅 직전) */
/* 진단 정보: 찾은 HD Audio 장치와 코덱, 쓰는 출력 핀 */
int hda_info(char (*lines)[64], int max);
/* 소리 진단 (2.8.1): 스트림 상태, 쓰는 핀과 DAC까지 경로의 전원·앰프·EAPD (코덱에서 읽음) */
int hda_sound_diag(char (*lines)[64], int max);
int hda_gpio_next(void);                /* 소리 진단 G 키: GPIO 값 돌리기 (-1 못 함) */

/* kmain.c가 제공: 페이지를 캐시 안 됨으로, 밀리초 대기 */
void paging_set_uc(uintptr_t addr);
void delay_ms(uint32_t ms);
#endif
