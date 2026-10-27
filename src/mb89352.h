/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mb89352.h - 3.9.14 DEV-13: Fujitsu MB89352(A) SCSI 프로토콜 제어기 (MEGA-SCSI·WAVE-SCSI) + SCSI 하드 디스크 (ID 0)
 * 레지스터 0x0~0xE (BDID·SCTL·SCMD·INTS·PSNS·SSTS·SERR·PCTL·MBC·DREG·TEMP·TCH·TCM·TCL), 이니시에이터 전용
 * 지원: 고르기(Select), 명령·자료·상태·메시지 단계, 하드웨어 전송(Transfer), ACK/REQ 직접 조작, ATN, 버스 리셋
 * SCSI 하드 디스크: 섹터는 IDE 장치(ide.h)의 콜백·크기를 그대로 씀 (같은 하드 디스크 이미지)
 * 동작은 blueMSX(MB89352.c, ScsiDevice.c)와 openMSX(MB89352·SCSIHD)를 보고 X86MSX 구조로 새로 작성 (코드 복사 없음) */
#ifndef MSX_MB89352_H
#define MSX_MB89352_H
#include <stdint.h>
#include "ide.h"

enum { SPC_BUF = 0x4000 };

typedef struct {
    uint8_t regs[16];
    uint8_t buf[SPC_BUF];
    uint8_t cdb[12];
    uint8_t phase, next_phase;  /* SPC_PH_* */
    uint8_t my_id, target_id;
    uint8_t rst, atn, enabled, busy, xfer;
    int msgin, counter;
    uint32_t cdb_idx, buf_idx, block_counter, tc;
    /* 하드 디스크(목표 장치) */
    uint32_t cur_sector, cur_len, keycode;
    uint8_t message, lun;
} MB89352;

void spc_reset(MB89352 *s, int scsi_reset);
uint8_t spc_read_reg(MB89352 *s, IDE *d, int reg);
void spc_write_reg(MB89352 *s, IDE *d, int reg, uint8_t v);
uint8_t spc_read_dreg(MB89352 *s, IDE *d);
void spc_write_dreg(MB89352 *s, IDE *d, uint8_t v);
#endif
