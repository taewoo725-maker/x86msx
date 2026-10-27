/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mb89352.c - 3.9.14 DEV-13: MB89352 SCSI 제어기 + SCSI 하드 디스크 (설명은 mb89352.h) */
#include "mb89352.h"
#include <string.h>

enum { R_BDID = 0, R_SCTL = 1, R_SCMD = 2, R_INTS = 4, R_PSNS = 5, R_SDGC = 5, R_SSTS = 6, R_SERR = 7, R_PCTL = 8, R_MBC = 9,
       R_DREG = 10, R_TEMP = 11, R_TCH = 12, R_TCM = 13, R_TCL = 14, R_TEMPWR = 13, R_FIXPCTL = 14 };
enum { P_IO = 0x01, P_CD = 0x02, P_MSG = 0x04, P_BSY = 0x08, P_SEL = 0x10, P_ATN = 0x20, P_ACK = 0x40, P_REQ = 0x80 };
enum { P_DATAIN = P_IO, P_DATAOUT = 0, P_COMMAND = P_CD, P_STATUS = P_CD | P_IO, P_MSGIN = P_MSG | P_CD | P_IO, P_MSGOUT = P_MSG | P_CD };
enum { I_RESET = 0x01, I_TIMEOUT = 0x04, I_SERVICE = 0x08, I_COMPLETE = 0x10, I_DISCONN = 0x20 };
enum { C_BUSREL = 0x00, C_SELECT = 0x20, C_RSTATN = 0x40, C_SETATN = 0x60, C_XFER = 0x80, C_XFERP = 0xA0, C_RSTACK = 0xC0, C_SETACK = 0xE0, C_MASK = 0xE0 };
enum { PH_UNDEF, PH_FREE, PH_COMMAND, PH_EXEC, PH_DATAIN, PH_DATAOUT, PH_STATUS, PH_MSGOUT, PH_MSGIN };

#define SENSE_NOMEDIUM  0x023A00u
#define SENSE_READERR   0x031100u
#define SENSE_WRFAULT   0x040300u
#define SENSE_BADCMD    0x052000u
#define SENSE_BADLBA    0x052100u
#define SENSE_BADLUN    0x052500u
#define SENSE_WRPROT    0x072700u
#define SENSE_INITERR   0x0B4800u
#define SENSE_REJECT    0x0B4300u
#define BUF_BLOCKS      (SPC_BUF / 512)

static int present(const IDE *d) { return d && d->present && d->sectors && d->rd; }

/* ---------------- SCSI 하드 디스크 (ID 0) ---------------- */
static void dev_bus_reset(MB89352 *s) { s->keycode = 0; }

static int check_addr(MB89352 *s, IDE *d)
{
    if (!present(d)) { s->keycode = SENSE_NOMEDIUM; return 0; }
    if (s->cur_len > 0 && s->cur_sector + s->cur_len <= d->sectors) return 1;
    s->keycode = SENSE_BADLBA;
    return 0;
}

static uint32_t read_sectors(MB89352 *s, IDE *d, uint32_t *blocks)
{
    uint32_t n = s->cur_len < BUF_BLOCKS ? s->cur_len : BUF_BLOCKS;
    uint32_t counter = s->cur_len * 512;
    for (uint32_t i = 0; i < n; i++) {
        if (d->rd(d->ctx, s->cur_sector, s->buf + i * 512)) { *blocks = 0; s->keycode = SENSE_READERR; return 0; }
        s->cur_sector++; s->cur_len--;
    }
    *blocks = s->cur_len;
    return counter;
}

static uint32_t write_sectors(MB89352 *s, IDE *d, uint32_t *blocks)
{
    uint32_t n = s->cur_len < BUF_BLOCKS ? s->cur_len : BUF_BLOCKS;
    for (uint32_t i = 0; i < n; i++) {
        if (!d->wr || d->wr(d->ctx, s->cur_sector, s->buf + i * 512)) { s->keycode = SENSE_WRFAULT; *blocks = 0; return 0; }
        s->cur_sector++; s->cur_len--;
    }
    uint32_t tmp = s->cur_len < BUF_BLOCKS ? s->cur_len : BUF_BLOCKS;
    *blocks = s->cur_len - tmp;
    return tmp * 512;
}

static uint32_t dev_data_in(MB89352 *s, IDE *d, uint32_t *blocks)
{
    if (s->cdb[0] == 0x28) { uint32_t c = read_sectors(s, d, blocks); if (c) return c; }
    *blocks = 0;
    return 0;
}

static uint32_t dev_data_out(MB89352 *s, IDE *d, uint32_t *blocks)
{
    if (s->cdb[0] == 0x2A) return write_sectors(s, d, blocks);
    *blocks = 0;
    return 0;
}

static uint32_t inquiry(MB89352 *s)
{
    uint32_t len = s->cur_len;
    if (!len) return 0;
    static const char id[] = "X86MSX  SCSI Harddisk  010a";    /* 제조사 8 + 제품 16 + 판 4 */
    memset(s->buf, 0, 56);
    s->buf[0] = 0; s->buf[1] = 0; s->buf[2] = 2; s->buf[3] = 2; s->buf[4] = 51;
    memcpy(s->buf + 8, id, 28);
    for (int i = 36; i < 56; i++) s->buf[i] = ' ';
    return len > 56 ? 56 : len;
}

static uint32_t mode_sense(MB89352 *s, IDE *d)
{
    if (s->cur_len > 0 && s->cdb[2] == 3) {
        uint32_t total = present(d) ? d->sectors : 0;
        uint8_t *b = s->buf;
        uint32_t size = 4 + 24;
        memset(b + 2, 0, 34);
        b[1] = total ? 0x00 : 0x70; b[3] = 8; b += 4;
        if (!(s->cdb[1] & 0x08)) { b[1] = (uint8_t)(total >> 16); b[2] = (uint8_t)(total >> 8); b[3] = (uint8_t)total; b[6] = 2; b += 8; size += 8; }
        b[0] = 3; b[1] = 0x16; b[3] = 8; b[11] = 64; b[12] = 2; b[20] = 0x80;
        s->buf[0] = (uint8_t)(size - 1);
        return s->cur_len < size ? s->cur_len : size;
    }
    s->keycode = SENSE_BADCMD;
    return 0;
}

static uint32_t request_sense(MB89352 *s)
{
    uint32_t len = s->cur_len, k = s->keycode;
    s->keycode = 0;
    memset(s->buf + 1, 0, 17);
    if (!len) return 0;                                 /* SCSI-2: 길이 0이면 보낼 것 없음 */
    s->buf[0] = 0x70; s->buf[2] = (uint8_t)(k >> 16); s->buf[7] = 10; s->buf[12] = (uint8_t)(k >> 8); s->buf[13] = (uint8_t)k;
    return len > 18 ? 18 : len;
}

/* 명령 실행: 반환 = 보낼·받을 바이트 수, *phase = 다음 단계 */
static uint32_t dev_execute(MB89352 *s, IDE *d, uint8_t *phase, uint32_t *blocks)
{
    uint8_t *c = s->cdb;
    uint32_t n;
    s->message = 0;
    *phase = PH_STATUS;
    *blocks = 0;
    if (((c[1] & 0xE0) || s->lun) && c[0] != 0x03 && !(c[0] == 0x12)) { s->keycode = SENSE_BADLUN; return 0; }
    if (c[0] != 0x03) s->keycode = 0;
    if (c[0] < 0x20) {
        s->cur_sector = (uint32_t)(c[1] & 0x1F) << 16 | c[2] << 8 | c[3];
        s->cur_len = c[4];
        switch (c[0]) {
        case 0x00: return 0;                            /* TEST UNIT READY */
        case 0x12: n = inquiry(s); if (n) *phase = PH_DATAIN; return n;
        case 0x03: n = request_sense(s); if (n) *phase = PH_DATAIN; return n;
        case 0x08:                                      /* READ(6) */
            if (!s->cur_len) s->cur_len = 256;
            if (check_addr(s, d)) { n = read_sectors(s, d, blocks); if (n) { c[0] = 0x28; *phase = PH_DATAIN; return n; } }
            return 0;
        case 0x0A: {                                    /* WRITE(6) */
            if (!s->cur_len) s->cur_len = 256;
            if (check_addr(s, d)) {
                if (!d->wr) { s->keycode = SENSE_WRPROT; return 0; }
                uint32_t tmp = s->cur_len < BUF_BLOCKS ? s->cur_len : BUF_BLOCKS;
                *blocks = s->cur_len - tmp;
                c[0] = 0x2A; *phase = PH_DATAOUT;
                return tmp * 512;
            }
            return 0;
        }
        case 0x0B: s->cur_len = 1; check_addr(s, d); return 0;   /* SEEK(6) */
        case 0x1A: n = mode_sense(s, d); if (n) *phase = PH_DATAIN; return n;
        case 0x04:                                      /* FORMAT UNIT: 첫 섹터만 지움 */
            if (!present(d)) { s->keycode = SENSE_NOMEDIUM; return 0; }
            if (!d->wr) { s->keycode = SENSE_WRPROT; return 0; }
            memset(s->buf, 0, 512);
            if (d->wr(d->ctx, 0, s->buf)) s->keycode = SENSE_WRFAULT;
            return 0;
        case 0x01: case 0x07: case 0x16: case 0x17: case 0x1B: case 0x1D: return 0;
        default: break;
        }
    } else {
        s->cur_sector = (uint32_t)c[2] << 24 | (uint32_t)c[3] << 16 | c[4] << 8 | c[5];
        s->cur_len = (uint32_t)(c[7] << 8 | c[8]);
        switch (c[0]) {
        case 0x28:                                      /* READ(10) */
            if (check_addr(s, d)) { n = read_sectors(s, d, blocks); if (n) { *phase = PH_DATAIN; return n; } }
            return 0;
        case 0x2A: {                                    /* WRITE(10) */
            if (check_addr(s, d)) {
                if (!d->wr) { s->keycode = SENSE_WRPROT; return 0; }
                uint32_t tmp = s->cur_len < BUF_BLOCKS ? s->cur_len : BUF_BLOCKS;
                *blocks = s->cur_len - tmp;
                *phase = PH_DATAOUT;
                return tmp * 512;
            }
            return 0;
        }
        case 0x25: {                                    /* READ CAPACITY */
            if (!present(d)) { s->keycode = SENSE_NOMEDIUM; return 0; }
            uint32_t last = d->sectors - 1;
            s->buf[0] = (uint8_t)(last >> 24); s->buf[1] = (uint8_t)(last >> 16); s->buf[2] = (uint8_t)(last >> 8); s->buf[3] = (uint8_t)last;
            s->buf[4] = 0; s->buf[5] = 0; s->buf[6] = 2; s->buf[7] = 0;
            *phase = PH_DATAIN;
            return 8;
        }
        case 0x2B: s->cur_len = 1; check_addr(s, d); return 0;   /* SEEK(10) */
        default: break;
        }
    }
    s->keycode = SENSE_BADCMD;
    return 0;
}

/* 메시지 아웃 → 비트: 0 메시지 인 있음, 1 ATN이 안 풀렸으면 버스 비움, 2 상태 단계로 (오류), -1 버스 비움 */
static int dev_msg_out(MB89352 *s, uint8_t v)
{
    if (v & 0x80) { s->lun = v & 7; return 0; }
    switch (v) {
    case 0x05: s->keycode = SENSE_INITERR; return 6;    /* INITIATOR DETECTED ERROR */
    case 0x0C: dev_bus_reset(s); return -1;             /* BUS DEVICE RESET */
    case 0x06: return -1;                               /* ABORT */
    case 0x07: case 0x09: case 0x08: return 2;          /* REJECT, PARITY ERROR, NO OPERATION */
    default: break;
    }
    s->message = 0x07;
    return (v >= 0x04 && v <= 0x11) ? 3 : 1;
}

/* ---------------- SPC ---------------- */
static void disconnect(MB89352 *s)
{
    if (s->phase != PH_FREE) {
        s->regs[R_INTS] |= I_DISCONN;
        s->phase = PH_FREE; s->next_phase = PH_UNDEF;
    }
    s->regs[R_PSNS] = 0;
    s->busy = 0; s->xfer = 0; s->counter = 0; s->tc = 0; s->atn = 0;
}

static void soft_reset(MB89352 *s)
{
    s->enabled = 0;
    for (int i = 2; i < 15; i++) s->regs[i] = 0;
    s->regs[15] = 0xFF;
    memset(s->cdb, 0, sizeof s->cdb);
    s->cdb_idx = 0; s->buf_idx = 0;
    s->phase = PH_FREE;
    disconnect(s);
}

void spc_reset(MB89352 *s, int scsi_reset)
{
    s->regs[R_BDID] = 0x80; s->regs[R_SCTL] = 0x80;
    s->rst = 0; s->atn = 0; s->my_id = 7;
    s->msgin = 0; s->block_counter = 0; s->target_id = 0; s->next_phase = PH_UNDEF;
    soft_reset(s);
    if (scsi_reset) { s->cur_sector = s->cur_len = 0; s->lun = 0; dev_bus_reset(s); }
}

static void set_ackreq(MB89352 *s, uint8_t *value)
{
    if ((s->regs[R_PSNS] & (P_REQ | P_BSY)) != (P_REQ | P_BSY)) { if (s->regs[R_PSNS] & P_IO) *value = 0xFF; return; }
    if (s->regs[R_FIXPCTL] != (s->regs[R_PSNS] & 7)) {
        if (s->regs[R_PSNS] & P_IO) *value = 0xFF;
        if (s->xfer) s->regs[R_INTS] |= I_SERVICE;
        return;
    }
    switch (s->phase) {
    case PH_DATAIN: *value = s->buf[s->buf_idx++]; s->regs[R_PSNS] = P_ACK | P_BSY | P_DATAIN; break;
    case PH_DATAOUT: s->buf[s->buf_idx++] = *value; s->regs[R_PSNS] = P_ACK | P_BSY | P_DATAOUT; break;
    case PH_COMMAND:
        if (s->counter < 0) { s->cdb_idx = 0; s->counter = *value < 0x20 ? 6 : (*value < 0xA0 ? 10 : 12); }
        if (s->cdb_idx < 12) s->cdb[s->cdb_idx] = *value;
        s->cdb_idx++;
        s->regs[R_PSNS] = P_ACK | P_BSY | P_COMMAND;
        break;
    case PH_STATUS: *value = s->keycode ? 2 : 0; s->regs[R_PSNS] = P_ACK | P_BSY | P_STATUS; break;
    case PH_MSGIN: *value = s->message; s->message = 0; s->regs[R_PSNS] = P_ACK | P_BSY | P_MSGIN; break;
    case PH_MSGOUT: s->msgin |= dev_msg_out(s, *value); s->regs[R_PSNS] = P_ACK | P_BSY | P_MSGOUT; break;
    default: break;
    }
}

static void set_psns_phase(MB89352 *s)
{
    switch (s->phase) {
    case PH_COMMAND: s->regs[R_PSNS] = P_REQ | P_BSY | P_COMMAND; break;
    case PH_DATAIN: s->regs[R_PSNS] = P_REQ | P_BSY | P_DATAIN; break;
    case PH_DATAOUT: s->regs[R_PSNS] = P_REQ | P_BSY | P_DATAOUT; break;
    case PH_STATUS: s->regs[R_PSNS] = P_REQ | P_BSY | P_STATUS; break;
    case PH_MSGIN: s->regs[R_PSNS] = P_REQ | P_BSY | P_MSGIN; break;
    default: break;
    }
}

static void reset_ackreq(MB89352 *s, IDE *d)
{
    if ((s->regs[R_PSNS] & (P_ACK | P_BSY)) != (P_ACK | P_BSY)) return;
    if (s->regs[R_FIXPCTL] != (s->regs[R_PSNS] & 7)) { if (s->xfer) s->regs[R_INTS] |= I_SERVICE; return; }
    switch (s->phase) {
    case PH_DATAIN:
        if (--s->counter > 0) s->regs[R_PSNS] = P_REQ | P_BSY | P_DATAIN;
        else {
            if (s->block_counter > 0) {
                s->counter = (int)dev_data_in(s, d, &s->block_counter);
                if (s->counter) { s->regs[R_PSNS] = P_REQ | P_BSY | P_DATAIN; s->buf_idx = 0; break; }
            }
            s->regs[R_PSNS] = P_REQ | P_BSY | P_STATUS; s->phase = PH_STATUS;
        }
        break;
    case PH_DATAOUT:
        if (--s->counter > 0) s->regs[R_PSNS] = P_REQ | P_BSY | P_DATAOUT;
        else {
            s->counter = (int)dev_data_out(s, d, &s->block_counter);
            if (s->counter) { s->regs[R_PSNS] = P_REQ | P_BSY | P_DATAOUT; s->buf_idx = 0; break; }
            s->regs[R_PSNS] = P_REQ | P_BSY | P_STATUS; s->phase = PH_STATUS;
        }
        break;
    case PH_COMMAND:
        if (--s->counter > 0) s->regs[R_PSNS] = P_REQ | P_BSY | P_COMMAND;
        else {
            uint8_t ph;
            s->buf_idx = 0;
            s->counter = (int)dev_execute(s, d, &ph, &s->block_counter);
            s->phase = ph;
            switch (ph) {
            case PH_DATAIN: s->regs[R_PSNS] = P_REQ | P_BSY | P_DATAIN; break;
            case PH_DATAOUT: s->regs[R_PSNS] = P_REQ | P_BSY | P_DATAOUT; break;
            case PH_STATUS: s->regs[R_PSNS] = P_REQ | P_BSY | P_STATUS; break;
            default: break;
            }
        }
        break;
    case PH_STATUS:
        s->regs[R_PSNS] = P_REQ | P_BSY | P_MSGIN; s->phase = PH_MSGIN;
        break;
    case PH_MSGIN:
    case PH_MSGOUT:
        if (s->phase == PH_MSGIN) {
            if (s->msgin <= 0) { disconnect(s); break; }
            s->msgin = 0;
        }
        if (s->msgin == -1) { disconnect(s); return; }
        if (s->atn) {
            if (s->msgin & 2) { disconnect(s); return; }
            s->regs[R_PSNS] = P_REQ | P_BSY | P_MSGOUT;
            return;
        }
        if (s->msgin & 1) s->phase = PH_MSGIN;
        else if (s->msgin & 4) { s->phase = PH_STATUS; s->next_phase = PH_UNDEF; }
        else { s->phase = s->next_phase; s->next_phase = PH_UNDEF; }
        s->msgin = 0;
        set_psns_phase(s);
        return;
    default: break;
    }
    if (s->atn) {
        s->next_phase = s->phase;
        s->phase = PH_MSGOUT;
        s->regs[R_PSNS] = P_REQ | P_BSY | P_MSGOUT;
    }
}

uint8_t spc_read_dreg(MB89352 *s, IDE *d)
{
    if (s->xfer && s->tc > 0) {
        set_ackreq(s, &s->regs[R_DREG]);
        reset_ackreq(s, d);
        if (--s->tc == 0) { s->xfer = 0; s->regs[R_INTS] |= I_COMPLETE; }
        s->regs[R_MBC] = (uint8_t)((s->regs[R_MBC] - 1) & 0x0F);
        return s->regs[R_DREG];
    }
    return 0xFF;
}

void spc_write_dreg(MB89352 *s, IDE *d, uint8_t v)
{
    if (s->xfer && s->tc > 0) {
        set_ackreq(s, &v);
        reset_ackreq(s, d);
        if (--s->tc == 0) { s->xfer = 0; s->regs[R_INTS] |= I_COMPLETE; }
        s->regs[R_MBC] = (uint8_t)((s->regs[R_MBC] - 1) & 0x0F);
    }
}

static uint8_t get_ssts(const MB89352 *s)
{
    uint8_t r = 1;                                      /* FIFO 빔 */
    if (s->xfer && (s->regs[R_PSNS] & P_IO)) { if (s->tc >= 8) r = 2; else if (s->tc) r = 0; }
    if (s->phase != PH_FREE) r |= 0x80;
    if (s->busy) r |= 0x20;
    if (s->phase >= PH_COMMAND || s->xfer) r |= 0x10;
    if (s->rst) r |= 0x08;
    if (!s->tc) r |= 0x04;
    return r;
}

void spc_write_reg(MB89352 *s, IDE *d, int reg, uint8_t v)
{
    switch (reg) {
    case R_DREG: spc_write_dreg(s, d, v); break;
    case R_SCMD:
        if (!s->enabled) break;
        if (v & 0x10) {
            if (!(s->regs[R_SCMD] & 0x10) && s->regs[R_SCTL] == 0) { s->rst = 1; s->regs[R_INTS] |= I_RESET; dev_bus_reset(s); disconnect(s); }
        } else s->rst = 0;
        s->regs[R_SCMD] = v;
        switch (v & C_MASK) {
        case C_SETACK:
            if (s->phase == PH_DATAIN || s->phase == PH_STATUS || s->phase == PH_MSGIN) set_ackreq(s, &s->regs[R_TEMP]);
            else set_ackreq(s, &s->regs[R_TEMPWR]);
            break;
        case C_RSTACK: reset_ackreq(s, d); break;
        case C_SELECT: {
            if (s->rst) { s->regs[R_INTS] |= I_TIMEOUT; break; }
            if (s->regs[R_PCTL] & 1) { s->regs[R_INTS] |= I_TIMEOUT; disconnect(s); break; }
            int err = 1;
            unsigned x = s->regs[R_BDID] & s->regs[R_TEMPWR];
            if (s->phase == PH_FREE && x && x != s->regs[R_TEMPWR]) {
                x = s->regs[R_TEMPWR] & (unsigned)~s->regs[R_BDID];
                s->target_id = 0;
                while ((x >>= 1) != 0) s->target_id++;      /* 번호가 큰 쪽 우선 */
                if (s->target_id == 0 && present(d)) {      /* 목표 장치는 ID 0 하나 */
                    s->regs[R_INTS] |= I_COMPLETE;
                    s->busy = 1; s->msgin = 0; s->counter = -1; s->lun = 0; err = 0;
                    if (s->atn) { s->phase = PH_MSGOUT; s->next_phase = PH_COMMAND; s->regs[R_PSNS] = P_REQ | P_BSY | P_MSGOUT; }
                    else { s->phase = PH_COMMAND; s->next_phase = PH_UNDEF; s->regs[R_PSNS] = P_REQ | P_BSY | P_COMMAND; }
                }
            }
            if (err) { s->regs[R_INTS] |= I_TIMEOUT; disconnect(s); }
            break;
        }
        case C_XFER:
            if (s->regs[R_FIXPCTL] == (s->regs[R_PSNS] & 7) && (s->regs[R_PSNS] & (P_REQ | P_BSY))) s->xfer = 1;
            else s->regs[R_INTS] |= I_SERVICE;
            break;
        case C_BUSREL: disconnect(s); break;
        case C_SETATN: s->atn = P_ATN; break;
        case C_RSTATN: s->atn = 0; break;
        default: break;
        }
        break;
    case R_INTS:
        s->regs[R_INTS] &= (uint8_t)~v;
        if (s->rst) s->regs[R_INTS] |= I_RESET;
        break;
    case R_TEMP: s->regs[R_TEMPWR] = v; break;
    case R_TCL: s->tc = (s->tc & 0xFFFF00u) + v; break;
    case R_TCM: s->tc = (s->tc & 0xFF00FFu) + ((uint32_t)v << 8); break;
    case R_TCH: s->tc = (s->tc & 0x00FFFFu) + ((uint32_t)v << 16); break;
    case R_PCTL: s->regs[R_PCTL] = v; s->regs[R_FIXPCTL] = v & 7; break;
    case R_BDID: v &= 7; s->my_id = v; s->regs[R_BDID] = (uint8_t)(1u << v); break;
    case R_SDGC: case R_SSTS: case R_SERR: case R_MBC: case 15: break;
    case R_SCTL: {
        uint8_t flag = !(v & 0xE0);
        if (flag != s->enabled) { s->enabled = flag; if (!flag) soft_reset(s); }
        s->regs[reg] = v;
        break;
    }
    default: s->regs[reg] = v; break;
    }
}

uint8_t spc_read_reg(MB89352 *s, IDE *d, int reg)
{
    switch (reg) {
    case R_DREG: return spc_read_dreg(s, d);
    case R_PSNS:
        if (s->phase == PH_EXEC) {                      /* 하드 디스크는 바로 끝나므로 닿지 않지만 원본과 같게 */
            s->phase = PH_STATUS; s->regs[R_PSNS] = P_REQ | P_BSY | P_STATUS;
        }
        return s->regs[R_PSNS] | s->atn;
    case R_SSTS: return get_ssts(s);
    case R_TCH: return (uint8_t)(s->tc >> 16);
    case R_TCM: return (uint8_t)(s->tc >> 8);
    case R_TCL: return (uint8_t)s->tc;
    default: return s->regs[reg];
    }
}
