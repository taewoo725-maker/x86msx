/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * psg.h - AY-3-8910 PSG (설계서 §4.5)
 *
 * PSG 클럭 = CPU / 2 = 1,789,772.5 Hz. 내부 카운터는 PSG 클럭 / 8 = CPU 16 T-state마다 1틱.
 *   톤 주파수 = 클럭 / (16 x TP), 잡음 = 클럭 / (16 x NP), 엔벌로프 한 단계 = 2 x EP 틱
 * 출력: 48 kHz 모노 16비트. CPU 시간(T-state)에 맞춰 레지스터 쓰기 직전까지 미리 생성한다.
 */
#ifndef MSX_PSG_H
#define MSX_PSG_H

#include <stdint.h>

#define PSG_RATE        48000
#define PSG_FRAME_MAX   1024        /* 한 프레임(59.92 Hz) 최대 샘플 수 (보통 801) */

typedef struct {
    uint8_t reg[16];
    uint8_t sel;

    uint16_t tone_cnt[3];
    uint8_t tone_out[3];
    uint16_t noise_cnt;
    uint32_t lfsr;
    uint8_t noise_out;
    uint32_t env_cnt;
    int env_step, env_attack, env_hold, env_alt, env_holding;

    uint8_t click;                  /* PPI 포트 C 비트 7 (키 클릭) */
    uint8_t tone_seen;              /* 3.7.2: 이번 프레임 음원 소리 (키 클릭 빼고) 있었음 — 계기·아이콘용 */
    uint8_t vol_tone, vol_click;    /* 믹서: 음원·키 클릭 음량 (0~100 %, 기본 100) */

    uint64_t next_tick;             /* 다음 내부 틱의 CPU T-state */
    uint64_t next_sample_fp;        /* 다음 샘플의 CPU T-state (16.16 고정소수점) */
    int32_t acc;                    /* 현재 샘플 구간 합계 */
    int32_t acc_n;

    int16_t buf[PSG_FRAME_MAX];     /* 이번 프레임 샘플 */
    int buf_n;
} PSG;

void psg_reset(PSG *p, uint64_t now);
/* now(CPU T-state)까지 샘플 생성 */
void psg_sync(PSG *p, uint64_t now);
void psg_write(PSG *p, uint8_t reg, uint8_t v);
uint8_t psg_read_reg(const PSG *p, uint8_t reg);
void psg_set_click(PSG *p, int on);

#endif
