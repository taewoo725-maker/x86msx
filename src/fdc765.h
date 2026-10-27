/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fdc765.h - TC8566AF (NEC uPD765 호환) 디스크 컨트롤러: 파나소닉 디스크 ROM(57), turbo R(143) */
#ifndef MSX_FDC765_H
#define MSX_FDC765_H
#include <stdint.h>
#include "fdc.h"

typedef struct {
    int phase;
    uint8_t cmd[9]; int cmd_pos;
    uint8_t res[7]; int res_len, res_pos;
    uint8_t buf[512]; int pos;
    int us, hd, mt, sc;
    uint8_t C, H, R, N, eot, fill;
    uint8_t pcn[2];
    int int_pending; uint8_t int_st0;
    uint64_t ready_at;          /* 3.3.1 디스크 시간 흉내: 이때까지 실행 단계 RQM 없음 */
} FDC765;

void fdc765_reset(FDC765 *c);
uint8_t fdc765_msr(FDC765 *c);                          /* 주 상태 레지스터 */
uint8_t fdc765_read_data(FDC765 *c, FDC *f);
void fdc765_write_data(FDC765 *c, FDC *f, uint8_t v);
void fdc765_tc(FDC765 *c);
void fdc765_poll(FDC765 *c, uint64_t now);             /* 3.3.1: 시간 흉내 (now = T-state) */
#endif
