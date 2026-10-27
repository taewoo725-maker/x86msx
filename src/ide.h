/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* ide.h - ATA(IDE) 하드 디스크 (섹터는 콜백으로 읽고 씀) */
#ifndef MSX_IDE_H
#define MSX_IDE_H
#include <stdint.h>

typedef int (*IdeSectorFn)(void *ctx, uint32_t lba, uint8_t *buf512);   /* 성공 0 */

typedef struct IDE IDE;
struct IDE {
    int present;
    uint32_t sectors;
    uint16_t cyls, heads, spt, cur_heads, cur_spt;
    uint8_t error, features, count, lba0, lba1, lba2, head, status, devctl;
    int xfer;                   /* 0 없음, 1 장치→CPU, 2 CPU→장치 */
    uint32_t left;
    int pos;
    uint8_t buf[512];
    int dirty;
    uint8_t half, latch;        /* 3.4.1: 8비트 자료 포트 (GIDE) — 아래 바이트를 읽으면 위 바이트를 붙잡아 둠 */
    IdeSectorFn rd, wr;
    void *ctx;
    IDE *slave;                 /* 3.4.3 DEV-15: 마스터에만 (레지스터 쓰기는 둘 다, 읽기·명령·자료는 DEV 비트로 고른 쪽) */
};

void ide_attach(IDE *d, uint32_t sectors, IdeSectorFn rd, IdeSectorFn wr, void *ctx);   /* sectors 0 = 없음 */
void ide_reset(IDE *d);                                 /* 슬레이브도 함께 */
uint8_t ide_read_reg(IDE *d, int r);                    /* 1 오류 ~ 7 상태 */
void ide_write_reg(IDE *d, int r, uint8_t v);           /* 1 기능 ~ 7 명령 */
void ide_write_control(IDE *d, uint8_t v);
uint16_t ide_read_data(IDE *d);
void ide_write_data(IDE *d, uint16_t v);
uint8_t ide_read_data8(IDE *d);                        /* 3.4.1: 바이트씩 (아래, 위 차례) */
void ide_write_data8(IDE *d, uint8_t v);
#endif
