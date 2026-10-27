/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * fdc.h - WD2793 / MB8877A 계열 디스크 컨트롤러와 드라이브 2대 (설계서 §6.7)
 *
 * 디스크 이미지는 메모리에 있는 .dsk(섹터 연속 배열)다. 크기로 형식을 정한다:
 *   360 KiB = 80트랙 1면 9섹터, 720 KiB = 80트랙 2면 9섹터, 1.44 MiB = 80트랙 2면 18섹터,
 *   180 KiB = 40트랙 1면 9섹터, 320/640 KiB = 8섹터.
 * 명령은 즉시 처리하고, 데이터는 데이터 레지스터로 한 바이트씩 오간다(DRQ).
 */
#ifndef MSX_FDC_H
#define MSX_FDC_H
#include <stdint.h>

typedef struct {
    uint8_t *data;              /* 이미지 (없으면 NULL) */
    uint32_t size;
    int wp;                     /* 쓰기 보호 */
    int dirty;                  /* 바뀜 (프런트엔드가 저장 후 지움) */
    int sides, spt, tracks;     /* 형식 */
    int head;                   /* 헤드가 있는 물리 트랙 */
    uint32_t dlo, dhi;          /* 3.5.5: 바뀐 바이트 범위 [dlo, dhi) (dirty일 때만 뜻이 있음, 바뀐 곳만 저장) */
} FdcDrive;

/* 섹터를 썼음: dirty와 바뀐 범위 */
static inline void fdc_mark(FdcDrive *d, uint32_t off, uint32_t len)
{
    if (!d->dirty) { d->dlo = off; d->dhi = off + len; }
    else { if (off < d->dlo) d->dlo = off; if (off + len > d->dhi) d->dhi = off + len; }
    d->dirty = 1;
}

typedef struct {
    uint8_t status, track, sector, data, cmd;
    int drive, side, motor;
    int irq, drq;
    int busy_type;              /* 마지막 명령 종류 1~4 */
    int step_dir;
    /* 데이터 전송 */
    int xfer;                   /* 0 없음, 1 읽기, 2 쓰기, 3 주소 읽기, 4 트랙 쓰기 */
    uint8_t buf[6400];
    int pos, len, multi;
    uint64_t now;               /* 마지막으로 알려 준 시간 (T-state) */
    uint64_t last_access[2];    /* 드라이브별 마지막 데이터 접근 시간 (LED 깜박임) */
    FdcDrive drv[3];            /* 0 = A, 1 = B, 2 = 선택 없음 (항상 비어 있음) */
    int ndrives;                /* 3.2.0: 연결된 드라이브 수 (1이면 B는 없음 → 디스크 롬이 가상 B 드라이브로). 0 = 2대 */
    uint64_t ready_at;          /* 3.3.1 MENU3-06: 이 시간(T-state)에 섹터 준비(DRQ). 0 = 기다림 없음 */
} FDC;

/* 3.3.1 MENU3-06 디스크 시간 흉내: 켜면 섹터마다 실제 디스크처럼(300 rpm, 트랙 9섹터 → 약 22 ms) 기다린 뒤 DRQ */
void fdc_poll(FDC *f, uint64_t now);

void fdc_reset(FDC *f);
void fdc_insert(FDC *f, int d, uint8_t *data, uint32_t size, int wp);
void fdc_eject(FDC *f, int d);
uint8_t fdc_read(FDC *f, int reg, uint64_t now);           /* reg 0 상태, 1 트랙, 2 섹터, 3 데이터 */
void fdc_write(FDC *f, int reg, uint8_t v, uint64_t now);
#endif
