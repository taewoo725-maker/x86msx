/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * mididev.h - MIDI 기계(장치) 부품 틀 (5.2.50, MIDI 개편 2단계 — 계획 문서 MIDI개편_계획_MIDI단자에기계꽂기)
 *
 * "MSX 의 MIDI OUT 단자에 실제 MIDI 기계를 꽂는다": 단자에 꽂힌 기계 목록(최대 MIDI_DEV_MAX)이 있고, MSX 가 보낸 MIDI 바이트는
 * 채널별로(기계마다 받는 채널 비트) 꽂힌 기계 모두에게 간다 (실기의 THRU + Rx 채널). 기계 종류는 표 한 줄(midi_dev_class).
 *   - 소프트웨어 음원 기계(MT-32/CM-32L, GM 음원): gmsynth 인스턴스 i(기계 번호)로 소리를 냄 (5.2.51: 여러 대 동시, MT-32 엔진만 하나).
 *   - 바깥 기계(USB MIDI 장치, .mid 녹음기): 바이트를 큐에 모아 두고 프런트엔드가 프레임마다 가져감 (midibus_take).
 * 5.2.50 단계: 단자는 하나(MSX-MIDI·SFG 가 보낸 것 모두). 단자 여러 개·기계별 믹서 채널은 3단계.
 */
#ifndef MSX_MIDIDEV_H
#define MSX_MIDIDEV_H
#include <stdint.h>

enum { MDK_NONE, MDK_MT32, MDK_GM, MDK_USB, MDK_FILE, MDK_GS, MDK_N };   /* MDK_GS = Roland SC-55 계열 (GS SoundFont) */   /* 기계 종류 (ini 저장 번호 — 바꾸지 않고 뒤에만 붙임) */
#define MIDI_DEV_MAX 4
#define MIDI_DEV_Q   1024
/* MSX 쪽 MIDI OUT 단자 (바이트의 출처). 기계는 한 단자에 꽂히거나(port 0·1) 모든 단자의 것을 받음(MDP_ANY, 예전과 같음 = 기본) */
enum { MDP_MSXMIDI = 0, MDP_SFG = 1, MDP_MM = 2, MDP_N = 3, MDP_ANY = 255 };   /* MM = Philips NMS-1205 Music Module (MC6850) */
#define MIDI_T_SFG 0x80000000u                      /* 본체 midi_t 비트 31 = SFG 가 보낸 바이트 (시각은 아래 비트) — MSX 구조를 안 바꾸려고 */
#define MIDI_T_MM  0x40000000u                      /* 비트 30 = Music Module 이 보낸 바이트 */
#define MIDI_T_SRC (MIDI_T_SFG | MIDI_T_MM)

typedef struct {
    const char *key;            /* ini 글자 */
    const char *name;           /* 긴 이름 (설정 창) */
    const char *short_name;     /* 짧은 이름 (믹서 띠·신호 띠) */
    uint8_t soft;               /* 1 = 소프트웨어 음원 (gmsynth 가 소리) */
    uint8_t nopt;               /* 설정값(opt) 가짓수: MT-32 2(MT-32/CM-32L), GM 2(SoundFont/FM) */
} MidiDevClass;
extern const MidiDevClass midi_dev_class[MDK_N];

typedef struct {
    uint8_t kind;               /* MDK_* (MDK_NONE = 빈 자리) */
    uint8_t opt;                /* 종류별 설정 (MT-32: 0 MT-32 1 CM-32L, GM: 0 SoundFont(없으면 FM) 1 FM, GS: 0 SC-55 실기 롬(Nuked-SC55, 없으면 GS SoundFont) 1 GS SoundFont) */
    uint8_t vol, mute;          /* 음량 0~100, 끄기 (3단계 믹서 채널에서 씀) */
    int8_t pan;                 /* -100~100 */
    uint16_t rx_mask;           /* 받는 채널 비트 (비트 n = 채널 n+1). 0xFFFF = 모두 (THRU) */
    uint8_t active;             /* 소프트웨어 기계: 1 = 지금 gmsynth 를 움직이는 기계 */
    uint8_t port;               /* 꽂힌 단자 MDP_* (MDP_ANY = 모든 단자) */
    uint16_t lvl;               /* 이번 프레임 최대 진폭 (소프트웨어 기계, 믹서 계기) */
    uint16_t ch_on[16];         /* 채널마다 받은 Note On 수 (MIDI 믹서 창의 불) */
    uint8_t run;                /* 이 기계가 마지막으로 받은 채널 상태 바이트 (실행 상태 거르기) */
    char rom_ctl[64], rom_pcm[64];  /* MT-32/CM-32L 기계가 쓸 롬 파일 이름 (비면 자동: 롬 종류 이름으로 찾음) — 기계마다 따로 */
    /* 곡이 SysEx 로 보낸 화면 글자·그림 (mt32-pi 처럼 카드 LCD 에) */
    uint8_t sx[80]; uint8_t sxn, sx_over; /* 이 기계가 받는 SysEx (앞 80 바이트) */
    char lcd[33];               /* MT-32 표시 글자(20) / SC-55 글자(32), 0 끝 */
    uint16_t dot[16];           /* SC-55 점 그림 16x16 (비트 15 = 왼쪽) */
    uint16_t lcd_seq, dot_seq;  /* 바뀔 때마다 +1 (프런트엔드가 보인 시각을 잼) */
    uint32_t bytes;             /* 받은 바이트 수 (아이콘·계기) */
    uint8_t q[MIDI_DEV_Q];      /* 바깥 기계: 모은 바이트 */
    uint32_t qt[MIDI_DEV_Q];    /* 그 시각 (프레임 안 T-state) */
    uint16_t qn;
} MidiDev;

typedef struct {
    MidiDev dev[MIDI_DEV_MAX];
    int n;
    uint8_t run;                /* 단자에 흐르는 실행 상태 (채널 가르기용) */
    uint8_t sysex;              /* SysEx 안 */
    int8_t dat1;                /* 다음 자료 바이트의 차례 (0·1) */
    uint8_t def_on;             /* 꽂힌 기계가 없을 때 GT 내장 MIDI 의 기본 음원(음원 인스턴스 0)으로 소리 냄 — 프런트엔드가 정함 */
} MidiBus;

extern MidiBus midi_bus;        /* 단자 하나 (5.2.50) */

void midibus_clear(void);                                   /* 기계 모두 뺌 */
int  midibus_plug(int kind, int opt);                       /* 끝에 꽂음 (자리 번호, -1 = 자리 없음). rx_mask = 모두, 음량 100 */
void midibus_unplug(int i);
MidiDev *midibus_find(int kind);                            /* 그 종류 첫 기계 (0 = 없음) */
int  midibus_soft(void);                                    /* 첫 소프트웨어 기계 번호 (-1 = 없음). 5.2.51: 소프트웨어 기계 i 는 음원 인스턴스 i (gmi_*) */
int  midibus_def(void);                                     /* 기계가 없어 GT 내장 기본 음원(인스턴스 0)이 소리 내는 중 */
int  midibus_dev_soft_on(void);                             /* 꽂힌 기계 중 켜진 소프트웨어 음원 (GT 기본 음원 제외) */
int  midibus_any_soft_on(void);                             /* 켜진 소프트웨어 음원이 하나라도 있음 */
void midibus_feed(uint8_t b, uint32_t t, int src);          /* MSX 가 보낸 바이트 하나 (본체 소리 만들 때 시각 순서로 부름). src = 보낸 단자 MDP_* */
int  midibus_take(int i, uint8_t *out, uint32_t *out_t, int max);   /* 바깥 기계 큐 비우기 (프런트엔드) */
int  midibus_gm_mode(const MidiDev *d);
void midibus_sysex_done(MidiDev *d);                        /* 받은 SysEx 하나 풀기 (화면 글자·점 그림) — midibus_feed 가 부름, 시험용으로 밖에도 */                     /* 소프트웨어 기계가 바라는 gmsynth 방식 (GM_MT32 / GM_SF2 / GM_FM), 아니면 GM_OFF */
#endif
