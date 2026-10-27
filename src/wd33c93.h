/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* wd33c93.h - 3.4.1 DEV-14: WD33C93 SCSI 제어기 + SCSI 하드 디스크 하나 (ID 0, Gouda SCSI·Novaxis BIOS)
 *   포트 34h 쓰기 = 레지스터 번호, 읽기 = 보조 상태 / 35h = 레지스터 읽기·쓰기(번호 자동 증가) / 36h 쓰기 = 리셋
 *   지원 명령: 소프트 리셋(00h), 끊기(04h), 고르기(06·07h), 고르고 전송(08·09h)
 *   SCSI 명령: TEST UNIT READY, REZERO, REQUEST SENSE, FORMAT UNIT, READ(6·10), WRITE(6·10), SEEK, INQUIRY,
 *             MODE SENSE(페이지 3), START/STOP, READ CAPACITY 등
 * 섹터는 IDE 장치(ide.h)의 콜백·크기를 그대로 씀 (같은 하드 디스크 이미지)
 * 동작은 blueMSX(wd33c93.c, ScsiDevice.c)를 보고 X86MSX 구조로 새로 작성 */
#ifndef MSX_WD33C93_H
#define MSX_WD33C93_H
#include <stdint.h>
#include "ide.h"

typedef struct {
    uint8_t regs[32];
    uint8_t latch;
    uint8_t target;
    uint8_t phase;              /* 0 버스 비움, 1 자료 들어옴(장치→CPU), 2 자료 나감(CPU→장치) */
    uint8_t unit_att;           /* 리셋 뒤 첫 명령에 UNIT ATTENTION */
    uint8_t lun;
    uint8_t cdb[12];
    uint32_t tc;                /* 전송 카운터 */
    uint32_t sense;             /* 감지 키·코드·한정자 (0 = 이상 없음) */
    uint32_t lba, left;         /* 다음 섹터, 남은 섹터 (지금 버퍼 다음부터) */
    int count, pos;             /* 버퍼에 남은 바이트, 위치 */
    uint8_t buf[512];
} WD33C93;

void wd_reset(WD33C93 *w);
uint8_t wd_read_aux(WD33C93 *w);
uint8_t wd_read_ctrl(WD33C93 *w, IDE *disk);
void wd_write_adr(WD33C93 *w, uint8_t v);
void wd_write_ctrl(WD33C93 *w, IDE *disk, uint8_t v);
#endif
