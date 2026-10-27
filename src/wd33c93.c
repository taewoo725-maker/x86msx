/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* wd33c93.c - 3.4.1 DEV-14: WD33C93 SCSI 제어기 + SCSI 하드 디스크 (설명은 wd33c93.h) */
#include "wd33c93.h"
#include <string.h>
#ifdef WD_TRACE
#include <stdio.h>
#endif

enum { R_OWN_ID = 0x00, R_CDB1 = 0x03, R_CMD_PHASE = 0x10, R_TCH = 0x12, R_TCM = 0x13, R_TCL = 0x14, R_DST_ID = 0x15,
       R_SRC_ID = 0x16, R_STATUS = 0x17, R_CMD = 0x18, R_DATA = 0x19, R_AUX = 0x1F };
enum { AS_DBR = 0x01, AS_CIP = 0x10, AS_BSY = 0x20, AS_INT = 0x80 };
enum { SS_RESET = 0x00, SS_RESET_ADV = 0x01, SS_XFER_END = 0x16, SS_SEL_TIMEOUT = 0x42, SS_DISCONNECT = 0x85 };
enum { PH_FREE, PH_IN, PH_OUT };
#define SENSE_POWER_ON      0x062900u
#define SENSE_NO_MEDIUM     0x023A00u
#define SENSE_BAD_CMD       0x052000u
#define SENSE_BAD_LBA       0x052100u
#define SENSE_BAD_LUN       0x052500u
#define SENSE_READ_ERR      0x031100u
#define SENSE_WRITE_FAULT   0x040300u

static int present(const IDE *d) { return d && d->present && d->sectors && d->rd; }

void wd_reset(WD33C93 *w)
{
    memset(w, 0, sizeof *w);
    memset(w->regs + 0x1B, 0xFF, 4);
    w->regs[R_AUX] = AS_INT;
    w->unit_att = 1;
}

static void bus_free(WD33C93 *w)
{
    if (w->phase != PH_FREE) {
        if (w->regs[R_STATUS] != SS_XFER_END) w->regs[R_STATUS] = SS_DISCONNECT;
        w->regs[R_AUX] = AS_INT;
    }
    w->phase = PH_FREE;
    w->tc = 0;
}

static void finish(WD33C93 *w)                          /* 상태 단계 → 끝 (상태 코드는 TLUN 레지스터에) */
{
    w->regs[0x0F] = w->sense ? 0x02 : 0x00;             /* CHECK CONDITION / GOOD */
    w->regs[R_STATUS] = SS_XFER_END;
    w->phase = PH_FREE;
    w->tc = 0;
    w->regs[R_AUX] = AS_INT;
}

static int load_sector(WD33C93 *w, IDE *d)              /* 다음 섹터를 버퍼로 */
{
    if (d->rd(d->ctx, w->lba, w->buf)) { w->sense = SENSE_READ_ERR; return 0; }
    w->lba++; w->left--;
    w->count = 512; w->pos = 0;
    return 1;
}

static int check_range(WD33C93 *w, IDE *d)
{
    if (!present(d)) { w->sense = SENSE_NO_MEDIUM; return 0; }
    if (!w->left || w->lba + w->left > d->sectors) { w->sense = SENSE_BAD_LBA; return 0; }
    return 1;
}

static void data_in(WD33C93 *w, int n)                  /* 버퍼 n바이트를 내보냄 */
{
    w->count = n; w->pos = 0; w->left = 0;
    w->phase = PH_IN;
}

/* SCSI 명령 실행: 자료 단계면 phase를 바꾸고, 아니면 끝 */
static void execute(WD33C93 *w, IDE *d)
{
    const uint8_t *c = w->cdb;
    uint32_t sense_now = w->sense;
#ifdef WD_TRACE
    fprintf(stderr, "SCSI cmd %02X %02X %02X %02X %02X %02X  %02X %02X %02X ua %d lun %d\n", c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], w->unit_att, w->lun);
#endif
    if (w->unit_att && c[0] != 0x12 && c[0] != 0x03) {   /* 리셋 뒤 첫 명령: UNIT ATTENTION */
        w->unit_att = 0;
        w->sense = SENSE_POWER_ON;
        finish(w);
        return;
    }
    if (((c[1] & 0xE0) || w->lun) && c[0] != 0x03) { w->sense = SENSE_BAD_LUN; finish(w); return; }
    if (c[0] != 0x03) w->sense = 0;
    memset(w->buf, 0, sizeof w->buf);
    int group0 = c[0] < 0x20;
    w->lba = group0 ? ((uint32_t)(c[1] & 0x1F) << 16 | c[2] << 8 | c[3]) : ((uint32_t)c[2] << 24 | (uint32_t)c[3] << 16 | c[4] << 8 | c[5]);
    w->left = group0 ? c[4] : (uint32_t)(c[7] << 8 | c[8]);
    switch (c[0]) {
    case 0x00: break;                                   /* TEST UNIT READY */
    case 0x03: {                                        /* REQUEST SENSE */
        uint32_t k = w->unit_att ? SENSE_POWER_ON : sense_now;
        int n = c[4];
        w->unit_att = 0;
        w->sense = 0;
        if (!n) { w->buf[0] = (uint8_t)(k >> 8); data_in(w, 4); return; }
        w->buf[0] = 0x70; w->buf[2] = (uint8_t)(k >> 16); w->buf[7] = 10; w->buf[12] = (uint8_t)(k >> 8); w->buf[13] = (uint8_t)k;
        data_in(w, n > 18 ? 18 : n);
        return;
    }
    case 0x12: {                                        /* INQUIRY */
        static const char id[] = "X86MSX  SCSI HARD DISK  0341";
        int n = c[4] > 56 ? 56 : c[4];
        if (!n) break;
        w->buf[0] = 0x00; w->buf[1] = 0x80;             /* 직접 접근, 떼어낼 수 있음 (Novaxis가 그렇게 다룸) */
        w->buf[2] = 1; w->buf[3] = 1; w->buf[4] = 51;   /* SCSI-1, CCS */
        memcpy(w->buf + 8, id, 28);
        for (int i = 36; i < 56; i++) w->buf[i] = ' ';
        data_in(w, n);
        return;
    }
    case 0x1A:                                          /* MODE SENSE (페이지 3) */
        if (c[2] == 3 && c[4]) {
            uint32_t total = present(d) ? d->sectors : 0;
            int size = 4 + 24, n = c[4];
            uint8_t *b = w->buf;
            b[1] = total ? 0x00 : 0x70; b[3] = 8; b += 4;
            if (!(c[1] & 0x08)) { b[1] = (uint8_t)(total >> 16); b[2] = (uint8_t)(total >> 8); b[3] = (uint8_t)total; b[6] = 2; b += 8; size += 8; }
            b[0] = 3; b[1] = 0x16; b[3] = 8; b[11] = 64; b[12] = 2; b[20] = 0xA0;
            w->buf[0] = (uint8_t)(size - 1);
            data_in(w, n > size ? size : n);
            return;
        }
        w->sense = SENSE_BAD_CMD;
        break;
    case 0x25: {                                        /* READ CAPACITY */
        if (!present(d)) { w->sense = SENSE_NO_MEDIUM; break; }
        uint32_t last = d->sectors - 1;
        w->buf[0] = (uint8_t)(last >> 24); w->buf[1] = (uint8_t)(last >> 16); w->buf[2] = (uint8_t)(last >> 8); w->buf[3] = (uint8_t)last;
        w->buf[6] = 2;
        data_in(w, 8);
        return;
    }
    case 0x08: case 0x28:                               /* READ */
        if (c[0] == 0x08 && !w->left) w->left = 256;
        if (!check_range(w, d) || !load_sector(w, d)) break;
        w->phase = PH_IN;
        return;
    case 0x0A: case 0x2A:                               /* WRITE */
        if (c[0] == 0x0A && !w->left) w->left = 256;
        if (!check_range(w, d)) break;
        if (!d->wr) { w->sense = 0x072700u; break; }    /* 쓰기 금지 */
        w->count = 512; w->pos = 0;
        w->phase = PH_OUT;
        return;
    case 0x0B: case 0x2B:                               /* SEEK */
        w->left = 1; check_range(w, d); break;
    case 0x04:                                          /* FORMAT UNIT: 첫 섹터만 지움 */
        if (!present(d)) { w->sense = SENSE_NO_MEDIUM; break; }
        if (!d->wr || d->wr(d->ctx, 0, w->buf)) w->sense = SENSE_WRITE_FAULT;
        break;
    case 0x01: case 0x07: case 0x16: case 0x17: case 0x1B: case 0x1D: case 0x1E:   /* REZERO, REASSIGN, RESERVE, RELEASE, START/STOP, DIAG, 꺼냄 막기 */
        break;
    default:
        w->sense = SENSE_BAD_CMD;
        break;
    }
    finish(w);
}

static void exec_cmd(WD33C93 *w, IDE *d, uint8_t v)
{
    if (w->regs[R_AUX] & AS_CIP) return;
    w->regs[R_CMD] = v;
    switch (v) {
    case 0x00:                                          /* 소프트 리셋 */
        memset(w->regs + 1, 0, 0x1A);
        w->phase = PH_FREE; w->tc = 0; w->latch = 0;
        w->regs[R_AUX] = AS_INT;
        w->regs[R_STATUS] = (w->regs[R_OWN_ID] & 0x08) ? SS_RESET_ADV : SS_RESET;
        break;
    case 0x04: bus_free(w); break;                      /* 끊기 */
    case 0x06: case 0x07:                               /* 고르기만: 이 에뮬레이터는 시간 초과로 */
        w->target = w->regs[R_DST_ID] & 7;
        w->regs[R_STATUS] = SS_SEL_TIMEOUT; w->tc = 0; w->regs[R_AUX] = AS_INT;
        break;
    case 0x08: case 0x09:                               /* 고르고 전송 */
        w->target = w->regs[R_DST_ID] & 7;
        if (w->target == 0 && present(d)) {
            w->lun = (v == 0x08) ? (w->regs[0x0F] & 7) : 0;
            memcpy(w->cdb, w->regs + R_CDB1, 12);
            execute(w, d);
            if (w->phase != PH_FREE) w->regs[R_AUX] = AS_CIP | AS_BSY | AS_DBR;
        } else {
            w->tc = 0;
            w->regs[R_STATUS] = SS_SEL_TIMEOUT;
            w->regs[R_AUX] = AS_INT;
        }
        break;
    default: break;
    }
}

uint8_t wd_read_aux(WD33C93 *w) { return w->regs[R_AUX]; }

void wd_write_adr(WD33C93 *w, uint8_t v) { w->latch = v & 0x1F; }

void wd_write_ctrl(WD33C93 *w, IDE *d, uint8_t v)
{
    switch (w->latch) {
    case R_OWN_ID: w->regs[R_OWN_ID] = v; break;
    case R_TCH: w->tc = (w->tc & 0x00FFFF) | ((uint32_t)v << 16); break;
    case R_TCM: w->tc = (w->tc & 0xFF00FF) | ((uint32_t)v << 8); break;
    case R_TCL: w->tc = (w->tc & 0xFFFF00) | v; break;
    case R_CMD: exec_cmd(w, d, v); return;              /* 번호 증가 없음 */
    case R_DATA:
        w->regs[R_DATA] = v;
        if (w->phase == PH_OUT) {
            w->buf[w->pos++] = v; w->tc--;
            if (--w->count == 0) {
                if (d->wr(d->ctx, w->lba, w->buf)) { w->sense = SENSE_WRITE_FAULT; finish(w); return; }
                w->lba++; w->left--;
                if (w->left) { w->count = 512; w->pos = 0; }
                else finish(w);
            }
        }
        return;
    case R_AUX: return;
    default: if (w->latch <= R_SRC_ID) w->regs[w->latch] = v; break;
    }
    w->latch = (w->latch + 1) & 0x1F;
}

uint8_t wd_read_ctrl(WD33C93 *w, IDE *d)
{
    uint8_t r;
    switch (w->latch) {
    case R_STATUS:
        r = w->regs[R_STATUS];
        if (r != SS_XFER_END) w->regs[R_AUX] &= (uint8_t)~AS_INT;
        else { w->regs[R_STATUS] = SS_DISCONNECT; w->regs[R_AUX] = AS_INT; }   /* 끝 → 다음은 버스 비움 인터럽트 */
        break;
    case R_DATA:
        if (w->phase != PH_IN) return w->regs[R_DATA];
        r = w->buf[w->pos++]; w->regs[R_DATA] = r; w->tc--;
        if (--w->count == 0) {
            if (w->left) { if (!load_sector(w, d)) finish(w); }
            else finish(w);
        }
        return r;
    case R_TCH: r = (uint8_t)(w->tc >> 16); break;
    case R_TCM: r = (uint8_t)(w->tc >> 8); break;
    case R_TCL: r = (uint8_t)w->tc; break;
    case R_AUX: return wd_read_aux(w);
    default: r = w->regs[w->latch]; break;
    }
    if (w->latch != R_CMD) w->latch = (w->latch + 1) & 0x1F;
    return r;
}
