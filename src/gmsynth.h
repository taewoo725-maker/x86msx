/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* gmsynth.h - 3.4.5 SND3-07: 소프트웨어 MIDI 음원 (General MIDI, 48 kHz 스테레오, 실수 없음)
 *   MSX-MIDI·SFG(YM2148)가 보낸 MIDI 바이트를 받아 PC 소리로 낸다.
 *   방식 1 FM: 2연산자 FM으로 GM 128음색(16가족 바탕 + 프로그램마다 조금씩) + 채널 10 타악기 합성. 파일 없음.
 *   방식 2 SoundFont: 사용자가 넣은 .sf2 (RIFF sfbk, 16비트 샘플)를 읽어 재생. 데이터는 부르는 쪽 메모리 그대로 씀.
 *   전역 하나 (opl4 웨이브 메모리처럼), 상태 저장에는 넣지 않음 (불러오면 모든 음을 끔). */
#ifndef MSX_GMSYNTH_H
#define MSX_GMSYNTH_H
#include <stdint.h>

enum { GM_OFF, GM_FM, GM_SF2, GM_MT32, GM_SC55 };   /* GM_SC55 = Roland SC-55 실기 롬 에뮬 (Nuked-SC55, 엔진 하나) */      /* GM_MT32 = Roland MT-32 / CM-32L 롬 에뮬 (UEFI 판만) */
#define GM_INST_GT 4                             /* GT 내장 MIDI 의 기본 음원 = 마지막 인스턴스 (단자 기계 0~3 과 따로, 같이 울림) */
#define GM_INST_MAX 5                            /* 음원 인스턴스 수 (= 단자에 꽂을 수 있는 기계 수 MIDI_DEV_MAX). SoundFont 자료는 공용, MT-32 엔진은 하나 */
void gm_use(int i);                              /* 아래 gm_*() 가 다룰 인스턴스 고르기 (처음엔 0). gmi_*(i, …) 는 고르고 부르는 것 */
void gmi_reset(int i);
void gmi_set_mode(int i, int m);
void gmi_set_mt32(int i, int slot);               /* MT-32 엔진 슬롯 (0·1) — gmi_set_mode(i, GM_MT32) 전에 */
int  gmi_mode(int i);
void gmi_midi(int i, uint8_t b);
void gmi_render(int i, int32_t *l, int32_t *r, int n);
int  gmi_voices(int i);
extern unsigned gm_guard_hits;                  /* 폭주 방지가 값을 묶거나 비운 횟수 */
int  gmi_level(int i);                            /* 소리 나는 동안 평균 크기 (RMS, PSG 3음 화음 음량 13 ≈ 3600) */
int  gmi_trim(int i);                             /* 음량 맞춤 값 (Q8, 256 = 그대로) */
int  gm_any_on(void);                            /* 어느 인스턴스든 켜져 있음 */
#define GM_SF_MAX 2                              /* SoundFont 칸 (0 = GM 음원, 1 = GS 기계) */
void gm_sf_slot(int s);                          /* gm_load_sf2·_stream·gm_unload_sf2·gm_sf2_presets 가 다룰 칸 고르기 */
void gmi_set_sf(int i, int slot, int gs);        /* 인스턴스 i 가 쓸 SoundFont 칸, GS 기계(리듬 파트 SysEx 받음) */
int  gmi_drums(int i);                           /* 드럼 파트 채널 비트 */
int  gmi_master(int i);                          /* 마스터 음량 0~127 */
void gm_set_mode(int mode);                     /* GM_SF2인데 SoundFont가 없으면 FM */
int  gm_mode(void);                             /* 지금 쓰는 방식 */
int  gm_load_sf2(const uint8_t *data, uint32_t len);   /* 0 성공 (프리셋 수 > 0), -1 형식 오류. data는 계속 살아 있어야 함 */
/* 4.0.133: 아주 큰 SoundFont (수백 MB) — 악기 목록(pdta)만 pool 앞쪽에 읽고, 샘플은 소리 낼 때 rd로 파일에서 읽어 pool 나머지에 담아 둠
 * (가장 오래 전에 읽은 것부터 밀어냄). rd(offset, buf, len) 성공 0. fsize = 파일 크기. 0 성공, -1 형식 오류·pool 부족 */
typedef int (*gm_rd_fn)(uint32_t off, uint8_t *buf, uint32_t len);
int  gm_load_sf2_stream(gm_rd_fn rd, uint32_t fsize, uint8_t *pool, uint32_t cap);
void gm_unload_sf2(void);
int  gm_sf2_presets(void);                      /* 불러온 SoundFont의 프리셋 수 (0 = 없음) */
void gm_reset(void);                            /* 모든 음 끄고 채널 초기값 */
void gm_midi(uint8_t b);                        /* MIDI 바이트 하나 (실행 상태·SysEx 처리) */
void gm_render(int32_t *l, int32_t *r, int n);  /* n표본을 l·r에 씀 (±32767 안팎) */
/* MT-32 / CM-32L (롬은 사용자가 USB 의 MT32 폴더에). 롬 자료는 부르는 쪽 메모리에 계속 살아 있어야 함. arena = 엔진이 쓰는 4 MiB 이상 덩어리.
 * open: 0 성공, -1 알 수 없는 롬, -2 열기 실패, -4 메모리, -9 이 판에는 없음 (레거시) */
int  gm_mt32_open(int slot, void *arena, uint32_t arena_n, const uint8_t *ctl, uint32_t ctl_n, const uint8_t *pcm, uint32_t pcm_n);
void gm_mt32_close(int slot);
int  gm_mt32_ready(int slot);
const char *gm_mt32_name(int slot);
int  gm_voices(void);                           /* 울리는 목소리 수 */
void gm_fx_set(int rev_pct, int cho_pct, int len_pct);   /* 공간 음향 (리버브·코러스 세기·잔향 길이 %, 0~200, 100 = 곡 그대로) */
extern int gm_rev_pct, gm_cho_pct;
#endif
