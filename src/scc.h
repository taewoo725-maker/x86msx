/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* scc.h - 코나미 SCC 음원 (5채널 파형, 설계서 §6.6) */
#ifndef MSX_SCC_H
#define MSX_SCC_H
#include <stdint.h>

typedef struct {
    int8_t wave[5][32];         /* SCC 모드: 채널 5는 채널 4 파형을 씀, SCC-I 모드: 채널마다 따로 */
    uint16_t period[5];
    uint8_t vol[5], enable, deform;
    uint32_t phase[5];          /* 16.16 파형 위치 */
#define SCC_BUF_MAX 1024
    int16_t buf[SCC_BUF_MAX];   /* 이번 프레임 소리 (2.11: 레지스터를 쓸 때마다 그때까지 만들어 둠) */
    int pos;                    /* buf에 만든 샘플 수 */
    int32_t lp;                 /* 저역 통과 상태 */
    int8_t pan[5]; uint8_t st;  /* 4.0.131: 채널별 좌우 (-100 왼쪽 ~ +100 오른쪽), st = 1이면 좌우 따로 만듦 (Yamanooto) */
    int32_t lpl, lpr;           /* 좌우 저역 통과 상태 */
    int16_t bufl[SCC_BUF_MAX], bufr[SCC_BUF_MAX];
} SCC;

void scc_reset(SCC *s);
/* 4.0.131: Yamanooto 좌우 배치 (openMSX와 같이 0, -67, +67, -67, +67 %). scc_reset 뒤에 다시 불러야 함 */
void scc_set_stereo(SCC *s);
/* 좌우를 따로 만든 소리를 lout·rout에 넣고(덮어씀), 모노 합은 out에 더함 */
void scc_mix_st(SCC *s, int16_t *out, int16_t *lout, int16_t *rout, int n, int vol);
void scc_write(SCC *s, uint8_t off, uint8_t v);         /* off = 주소 & 0xFF (0x9800 기준) */
uint8_t scc_read(SCC *s, uint8_t off);
/* SCC-I(SCC+) 모드 레지스터 (0xB800 기준): 0x00~0x9F 파형 5채널, 0xA0~0xAF 주기·음량·켜기, 0xC0~0xDF 변형 */
void scc_write_plus(SCC *s, uint8_t off, uint8_t v);
uint8_t scc_read_plus(SCC *s, uint8_t off);
/* 이번 프레임 소리를 upto 샘플(48 kHz)까지 만들어 둔다 (레지스터를 쓰기 전에 부름) */
void scc_render(SCC *s, int upto);
/* 나머지를 만들고 n개 샘플을 out에 더한 뒤 다음 프레임 준비. vol = 0~100 % */
void scc_mix(SCC *s, int16_t *out, int n, int vol);
#endif
