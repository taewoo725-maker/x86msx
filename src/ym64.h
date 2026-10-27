/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* ym64.h - 3.7.0 YM64: YAMAHA SFG의 FM 음원. YM2164(OPP)를 기준으로 만들고 YM2151(OPM)도 같이 냄
 *   YM2164(OPP)는 YM2151(OPM)을 개선한 칩: 레지스터·소리는 같고 타이머 B가 OPM의 절반 빠르기.
 *   opp = 1 (YM2164, SFG-05·기본), 0 (YM2151, SFG-01). 믹서·아이콘 이름은 "OPP"
 *   (3.4.1 DEV-32 YM2151 구현을 3.7.0에서 YM64로 이름을 바꿈)
 *   8채널 × 연산자 4, 알고리즘 8, 되먹임, DT1·DT2·MUL, 엔벌로프(AR·D1R·D1L·D2R·RR, 키 스케일), LFO(톱니·사각·삼각·잡음, AM·PM),
 *   잡음(채널 8 C2), 타이머 A·B와 IRQ, 좌우 출력(RL)
 *   표본율 = 3579545 / 64 Hz. 실수 계산 없음 (표는 tools/mkopmtab.py)
 * YM2151 자료표의 동작을 바탕으로 X86MSX에서 새로 작성 */
#ifndef MSX_YM64_H
#define MSX_YM64_H
#include <stdint.h>

typedef struct {
    uint32_t phase, inc;
    int32_t env;                /* 감쇠 0~1023 (0.09375 dB) */
    uint8_t state;              /* 0 꺼짐, 1 어택, 2 디케이1, 3 디케이2, 4 릴리스 */
    uint8_t key;
    int32_t out;
} OpmOp;

typedef struct {
    uint8_t reg[256];
    uint8_t addr;
    OpmOp op[32];               /* 레지스터 차례: 0~7 M1, 8~15 M2, 16~23 C1, 24~31 C2 */
    int32_t fb[8][2];
    uint8_t status;             /* 비트 0 타이머 A, 1 타이머 B */
    uint32_t ta_cnt, tb_cnt;    /* 다음 넘침까지 남은 클록 */
    uint32_t eg_cnt;
    uint8_t eg_sub;
    uint32_t lfo_phase;         /* 위 8비트 = LFO 자리 */
    uint32_t lfo_cnt;
    int32_t lfo_am, lfo_pm;     /* 0~255, -128~127 */
    uint8_t lfo_noise;
    uint32_t noise_lfsr, noise_cnt;
    int32_t noise_out;
    uint8_t inc_dirty;
    uint8_t opp;                /* 3.7.0: 1 = YM2164(OPP) 타이머 B (2048클록 단위), 0 = YM2151(OPM) */
} YM64;

void opm_reset(YM64 *o);
void opm_write(YM64 *o, int port, uint8_t v);         /* 0 = 레지스터 번호, 1 = 자료 */
uint8_t opm_status(const YM64 *o);
void opm_advance(YM64 *o, uint32_t clocks);            /* 타이머 (클록 = 3.58 MHz) */
int opm_irq(const YM64 *o);
void opm_calc(YM64 *o, int32_t *l, int32_t *r);        /* 표본 하나 */
int opm_active(const YM64 *o);                         /* 울리는 연산자가 있으면 1 */
#endif
