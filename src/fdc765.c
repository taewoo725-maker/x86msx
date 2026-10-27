/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * fdc765.c - TC8566AF (NEC uPD765 호환) 디스크 컨트롤러 (파나소닉 디스크 ROM 57, turbo R 143)
 *
 * 드라이브와 이미지는 fdc.c의 FDC 구조체(drv[], motor, drive, last_access)를 같이 쓴다.
 * 레지스터(디스크 ROM 영역): 0x7FF2 DOR(쓰기: 비트 0~1 드라이브, 비트 2 리셋 해제, 비트 4~7 모터),
 *   0x7FF4 MSR(읽기), 0x7FF5 데이터(읽기·쓰기). DMA 없이 CPU가 바이트마다 주고받는다.
 */
#include "fdc765.h"
#include <string.h>

enum { PH_CMD, PH_EXEC_READ, PH_EXEC_WRITE, PH_EXEC_FORMAT, PH_RESULT };

static int cmd_len(uint8_t c)
{
    switch (c & 0x1F) {
    case 0x03: return 3;            /* SPECIFY */
    case 0x04: return 2;            /* SENSE DRIVE STATUS */
    case 0x05: case 0x06: case 0x09: case 0x0C: return 9;   /* WRITE / READ DATA */
    case 0x07: return 2;            /* RECALIBRATE */
    case 0x08: return 1;            /* SENSE INTERRUPT STATUS */
    case 0x0A: return 2;            /* READ ID */
    case 0x0D: return 6;            /* FORMAT TRACK */
    case 0x0F: return 3;            /* SEEK */
    default: return 1;
    }
}

void fdc765_reset(FDC765 *c)
{
    memset(c, 0, sizeof *c);
}

static FdcDrive *drv(FDC *f, int d) { return &f->drv[(d & 1) && f->ndrives == 1 ? 2 : (d & 1)]; }   /* 3.4.0: 드라이브가 1대면 B = 없는 드라이브 */
static int absent(FDC *f, int d) { return (d & 1) && f->ndrives == 1; }

static long sec_off(FdcDrive *v, int track, int side, int sec)
{
    if (!v->data || sec < 1 || sec > v->spt || track >= v->tracks || side >= v->sides) return -1;
    long off = (((long)track * v->sides + side) * v->spt + (sec - 1)) * 512;
    return off + 512 <= (long)v->size ? off : -1;
}

static void result7(FDC765 *c, uint8_t st0, uint8_t st1, uint8_t st2)
{
    c->res[0] = st0; c->res[1] = st1; c->res[2] = st2;
    c->res[3] = c->C; c->res[4] = c->H; c->res[5] = c->R; c->res[6] = c->N;
    c->res_len = 7; c->res_pos = 0;
    c->phase = PH_RESULT;
}

/* 지금 섹터를 버퍼로 (읽기). 실패하면 결과 단계 */
static int load(FDC765 *c, FDC *f)
{
    FdcDrive *v = drv(f, c->us);
    long off = sec_off(v, v->head, c->hd, c->R);
    if (off < 0) { result7(c, (uint8_t)(0x40 | c->hd << 2 | c->us), v->data ? 0x04 : 0x05, 0); return 0; }  /* 섹터 없음 */
    memcpy(c->buf, v->data + off, 512);
    c->pos = 0;
    f->last_access[c->us & 1] = f->now;
    return 1;
}

static void execute(FDC765 *c, FDC *f)
{
    uint8_t op = c->cmd[0] & 0x1F;
    c->us = c->cmd[1] & 3;
    c->hd = (c->cmd[1] >> 2) & 1;
    FdcDrive *v = drv(f, c->us);
    f->drive = c->us & 1;
    f->side = c->hd;
    switch (op) {
    case 0x03:                                          /* SPECIFY */
        c->phase = PH_CMD;
        break;
    case 0x04: {                                        /* SENSE DRIVE STATUS → ST3 */
        uint8_t st3 = (uint8_t)(c->us | c->hd << 2);
        if (v->data) st3 |= 0x20 | (v->sides > 1 ? 0x08 : 0);
        if (v->head == 0 && !absent(f, c->us)) st3 |= 0x10;   /* 없는 드라이브는 트랙 0 신호가 없음 */
        if (v->data && v->wp) st3 |= 0x40;
        c->res[0] = st3; c->res_len = 1; c->res_pos = 0; c->phase = PH_RESULT;
        break;
    }
    case 0x07:                                          /* RECALIBRATE */
        v->head = 0;
        c->pcn[c->us & 1] = 0;
        c->int_pending = 1; c->int_st0 = (uint8_t)(absent(f, c->us) ? 0x70 | c->us : 0x20 | c->us);   /* 없는 드라이브: 비정상 + 장치 확인 */
        c->phase = PH_CMD;
        break;
    case 0x0F:                                          /* SEEK */
        v->head = c->cmd[2];
        c->pcn[c->us & 1] = c->cmd[2];
        c->int_pending = 1; c->int_st0 = (uint8_t)(0x20 | c->us | c->hd << 2);
        c->phase = PH_CMD;
        break;
    case 0x08:                                          /* SENSE INTERRUPT STATUS */
        if (c->int_pending) {
            c->res[0] = c->int_st0; c->res[1] = c->pcn[c->int_st0 & 1];
            c->res_len = 2;
            c->int_pending = 0;
        } else { c->res[0] = 0x80; c->res_len = 1; }
        c->res_pos = 0; c->phase = PH_RESULT;
        break;
    case 0x0A:                                          /* READ ID */
        c->C = (uint8_t)v->head; c->H = (uint8_t)c->hd; c->R = 1; c->N = 2;
        result7(c, v->data ? (uint8_t)(c->us | c->hd << 2) : (uint8_t)(0x40 | c->us), v->data ? 0 : 0x01, 0);
        break;
    case 0x06: case 0x0C:                               /* READ DATA */
        c->C = c->cmd[2]; c->H = c->cmd[3]; c->R = c->cmd[4]; c->N = c->cmd[5]; c->eot = c->cmd[6];
        c->mt = (c->cmd[0] & 0x80) != 0;
        if (!v->data) { result7(c, (uint8_t)(0x48 | c->us), 0, 0); break; }   /* 준비 안 됨 */
        if (load(c, f)) { c->phase = PH_EXEC_READ; }
        break;
    case 0x05: case 0x09:                               /* WRITE DATA */
        c->C = c->cmd[2]; c->H = c->cmd[3]; c->R = c->cmd[4]; c->N = c->cmd[5]; c->eot = c->cmd[6];
        c->mt = (c->cmd[0] & 0x80) != 0;
        if (!v->data) { result7(c, (uint8_t)(0x48 | c->us), 0, 0); break; }
        if (v->wp) { result7(c, (uint8_t)(0x40 | c->us), 0x02, 0); break; }   /* 쓰기 보호 */
        if (sec_off(v, v->head, c->hd, c->R) < 0) { result7(c, (uint8_t)(0x40 | c->us), 0x04, 0); break; }
        c->pos = 0; c->phase = PH_EXEC_WRITE;
        break;
    case 0x0D:                                          /* FORMAT TRACK: 섹터마다 C H R N 4바이트 */
        if (!v->data || v->wp) { result7(c, (uint8_t)(0x40 | c->us), v->wp ? 0x02 : 0, 0); break; }
        c->N = c->cmd[2]; c->sc = c->cmd[3]; c->fill = c->cmd[5];
        c->pos = 0; c->phase = PH_EXEC_FORMAT;
        break;
    default:                                            /* 지원 안 함 → ST0 = 0x80 */
        c->res[0] = 0x80; c->res_len = 1; c->res_pos = 0; c->phase = PH_RESULT;
        break;
    }
}

/* 섹터 하나가 끝났을 때: MSX 기종(파나소닉 MSX2·2+·turbo R)은 섹터마다 TC가 들어와 정상 종료한다.
 * 결과의 R은 다음 섹터 (EOT였으면 다음 실린더의 1번). 드라이버는 512바이트를 읽고 바로 결과를 읽는다 */
static void next_sector(FDC765 *c, FDC *f)
{
    (void)f;
    if (c->R >= c->eot) {
        c->R = 1;
        if (c->mt && c->hd == 0) c->H = 1;
        else c->C++;
    } else c->R++;
    result7(c, (uint8_t)(c->us | c->hd << 2), 0, 0);
}

void fdc765_poll(FDC765 *c, uint64_t now)
{
    if (c->ready_at && (now >= c->ready_at || (c->phase != PH_EXEC_READ && c->phase != PH_EXEC_WRITE))) c->ready_at = 0;
}

uint8_t fdc765_msr(FDC765 *c)
{
    if (c->ready_at) return 0x30;                       /* 3.3.1: 섹터를 기다리는 중 (EXM CB, RQM 없음) */
    switch (c->phase) {
    case PH_CMD: return (uint8_t)(0x80 | (c->cmd_pos ? 0x10 : 0));              /* RQM, (명령 받는 중이면 CB) */
    case PH_EXEC_READ: return 0xF0;                                             /* RQM DIO EXM CB */
    case PH_EXEC_WRITE: case PH_EXEC_FORMAT: return 0xB0;                       /* RQM EXM CB */
    default: return 0xD0;                                                       /* RQM DIO CB (결과) */
    }
}

uint8_t fdc765_read_data(FDC765 *c, FDC *f)
{
    if (c->ready_at && c->phase == PH_EXEC_READ) return 0xFF;
    if (c->phase == PH_EXEC_READ) {
        uint8_t v = c->buf[c->pos++];
        if (c->pos >= 512) next_sector(c, f);
        return v;
    }
    if (c->phase == PH_RESULT) {
        uint8_t v = c->res[c->res_pos++];
        if (c->res_pos >= c->res_len) c->phase = PH_CMD;
        return v;
    }
    return 0xFF;
}

void fdc765_write_data(FDC765 *c, FDC *f, uint8_t v)
{
    FdcDrive *d = drv(f, c->us);
    switch (c->phase) {
    case PH_CMD:
        c->cmd[c->cmd_pos++] = v;
        if (c->cmd_pos >= cmd_len(c->cmd[0])) { c->cmd_pos = 0; execute(c, f); }
        break;
    case PH_EXEC_WRITE:
        if (c->ready_at) break;
        c->buf[c->pos++] = v;
        if (c->pos >= 512) {
            long off = sec_off(d, d->head, c->hd, c->R);
            if (off >= 0) { memcpy(d->data + off, c->buf, 512); fdc_mark(d, (uint32_t)off, 512); }
            f->last_access[c->us & 1] = f->now;
            next_sector(c, f);
        }
        break;
    case PH_EXEC_FORMAT:
        c->buf[c->pos++] = v;
        if (c->pos >= c->sc * 4) {                      /* C H R N 목록을 다 받음: 섹터를 채움 */
            for (int i = 0; i < c->sc; i++) {
                long off = sec_off(d, d->head, c->hd, c->buf[i * 4 + 2]);
                if (off >= 0) { memset(d->data + off, c->fill, 512); fdc_mark(d, (uint32_t)off, 512); }
            }
            c->C = (uint8_t)d->head; c->H = (uint8_t)c->hd; c->R = 1;
            result7(c, (uint8_t)(c->us | c->hd << 2), 0, 0);
        }
        break;
    default:
        break;
    }
}

/* 실행 중 강제 종료 (TC): 지금까지로 정상 끝 */
void fdc765_tc(FDC765 *c)
{
    if (c->phase == PH_EXEC_READ || c->phase == PH_EXEC_WRITE)
        result7(c, (uint8_t)(c->us | c->hd << 2), 0, 0);
}
