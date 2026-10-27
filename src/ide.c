/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * ide.c - ATA 하드 디스크 (PIO, LBA28·CHS, 512바이트 섹터)
 * 섹터 읽기·쓰기는 콜백으로 (프런트엔드가 USB 파일을 섹터 단위로 다룸, 이미지 전체를 메모리에 올리지 않음).
 * 명령: IDENTIFY DEVICE(EC), READ SECTORS(20/21), WRITE SECTORS(30/31), READ VERIFY(40/41),
 *       SET FEATURES(EF), INITIALIZE DEVICE PARAMETERS(91), RECALIBRATE(1x), SEEK(7x), DIAGNOSTICS(90),
 *       SET MULTIPLE(C6), IDLE·STANDBY 계열(E0~E5, 성공만)
 */
#include "ide.h"
#include <string.h>
#ifdef IDE_TRACE
#include <stdio.h>
#endif

#define ST_BSY 0x80
#define ST_DRDY 0x40
#define ST_DSC 0x10
#define ST_DRQ 0x08
#define ST_ERR 0x01

static void dev_reset(IDE *d)
{
    d->error = 0x01;                                    /* 진단 결과: 이상 없음 */
    d->count = 1; d->lba0 = 1; d->lba1 = 0; d->lba2 = 0; d->head = 0;
    d->status = d->present ? (ST_DRDY | ST_DSC) : 0x00;
    d->xfer = 0; d->pos = 0; d->half = 0;
}

void ide_attach(IDE *d, uint32_t sectors, IdeSectorFn rd, IdeSectorFn wr, void *ctx)
{
    d->present = sectors != 0;
    d->sectors = sectors;
    d->rd = rd; d->wr = wr; d->ctx = ctx;
    /* CHS 모양: 16 헤드, 63 섹터 */
    d->heads = 16; d->spt = 63;
    d->cyls = sectors / (16 * 63);
    if (d->cyls > 16383) d->cyls = 16383;
    if (!d->cyls) d->cyls = 1;
    d->cur_heads = d->heads; d->cur_spt = d->spt;
    dev_reset(d);
}

static uint32_t cur_lba(const IDE *d)
{
    if (d->head & 0x40) return ((uint32_t)(d->head & 0x0F) << 24) | ((uint32_t)d->lba2 << 16) | ((uint32_t)d->lba1 << 8) | d->lba0;
    uint32_t cyl = ((uint32_t)d->lba2 << 8) | d->lba1, h = d->head & 0x0F, s = d->lba0;   /* CHS */
    if (!s) return 0xFFFFFFFFu;
    return (cyl * d->cur_heads + h) * d->cur_spt + (s - 1);
}

static void set_lba(IDE *d, uint32_t lba)
{
    if (d->head & 0x40) {
        d->lba0 = (uint8_t)lba; d->lba1 = (uint8_t)(lba >> 8); d->lba2 = (uint8_t)(lba >> 16);
        d->head = (uint8_t)((d->head & 0xF0) | ((lba >> 24) & 0x0F));
    } else {
        uint32_t s = lba % d->cur_spt + 1, t = lba / d->cur_spt, h = t % d->cur_heads, c = t / d->cur_heads;
        d->lba0 = (uint8_t)s; d->lba1 = (uint8_t)c; d->lba2 = (uint8_t)(c >> 8);
        d->head = (uint8_t)((d->head & 0xF0) | (h & 0x0F));
    }
}

static void fail(IDE *d, uint8_t err) { d->error = err; d->status = ST_DRDY | ST_DSC | ST_ERR; d->xfer = 0; }

static void put_str(uint8_t *b, int word, int words, const char *s)     /* ATA 문자열: 단어마다 바이트 순서 바꿈 */
{
    for (int i = 0; i < words * 2; i++) {
        char c = *s ? *s++ : ' ';
        b[(word * 2) + (i ^ 1)] = (uint8_t)c;
    }
}

static void identify(IDE *d)
{
    uint8_t *b = d->buf;
    memset(b, 0, 512);
#define W(n, v) do { b[(n) * 2] = (uint8_t)(v); b[(n) * 2 + 1] = (uint8_t)((v) >> 8); } while (0)
    W(0, 0x0040);                                       /* 고정 디스크 */
    W(1, d->cyls); W(3, d->heads); W(6, d->spt);
    put_str(b, 10, 10, "X86MSX0001");                  /* 일련번호 */
    put_str(b, 23, 4, "1.0");                          /* 펌웨어 */
    put_str(b, 27, 20, "X86MSX IDE HARD DISK");        /* 모델 */
    W(47, 0x8001);                                      /* 다중 섹터 1 */
    W(49, 0x0200);                                      /* LBA 지원 */
    W(53, 0x0001);
    W(54, d->cyls); W(55, d->heads); W(56, d->spt);
    uint32_t chs = (uint32_t)d->cyls * d->heads * d->spt;
    W(57, chs & 0xFFFF); W(58, chs >> 16);
    W(60, d->sectors & 0xFFFF); W(61, d->sectors >> 16);   /* LBA 섹터 수 */
#undef W
    d->pos = 0; d->xfer = 3; d->left = 0;              /* 3 = IDENTIFY 전송 (주소 레지스터는 그대로) */
    d->status = ST_DRDY | ST_DSC | ST_DRQ;
}

static int load(IDE *d)
{
    uint32_t lba = cur_lba(d);
    if (lba >= d->sectors || !d->rd || d->rd(d->ctx, lba, d->buf)) { fail(d, 0x10); return 0; }   /* IDNF */
    d->pos = 0;
    d->status = ST_DRDY | ST_DSC | ST_DRQ;
    return 1;
}

static void command(IDE *d, uint8_t c)
{
#ifdef IDE_TRACE
    fprintf(stderr, "IDE cmd %02X head %02X lba %u count %u feat %02X\n", c, d->head, cur_lba(d), d->count, d->features);
#endif
    d->error = 0; d->half = 0;
    if (!d->present) { d->status = 0; return; }        /* 장치 없음 */
    switch (c) {
    case 0xEC: identify(d); break;
    case 0x20: case 0x21:                               /* READ SECTORS */
        d->left = d->count ? d->count : 256;
        d->xfer = 1;
        load(d);
        break;
    case 0x30: case 0x31:                               /* WRITE SECTORS */
        d->left = d->count ? d->count : 256;
        if (cur_lba(d) >= d->sectors) { fail(d, 0x10); break; }
        d->xfer = 2; d->pos = 0;
        d->status = ST_DRDY | ST_DSC | ST_DRQ;
        break;
    case 0x40: case 0x41:                               /* READ VERIFY: 확인만 */
        if (cur_lba(d) + (d->count ? d->count : 256) > d->sectors) fail(d, 0x10);
        else d->status = ST_DRDY | ST_DSC;
        break;
    case 0x91:                                          /* INITIALIZE DEVICE PARAMETERS */
        d->cur_heads = (d->head & 0x0F) + 1;
        d->cur_spt = d->count ? d->count : 1;
        d->status = ST_DRDY | ST_DSC;
        break;
    case 0x90: d->error = 0x01; d->status = ST_DRDY | ST_DSC; break;
    case 0xEF: case 0xC6: case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE5: case 0xE7:
        d->status = ST_DRDY | ST_DSC; break;
    default:
        if ((c & 0xF0) == 0x10 || (c & 0xF0) == 0x70) { d->status = ST_DRDY | ST_DSC; break; }   /* RECALIBRATE, SEEK */
        fail(d, 0x04);                                  /* ABRT */
        break;
    }
}

static uint8_t dev_read_reg(IDE *d, int r)
{
    switch (r & 7) {
    case 1: return d->error;
    case 2: return d->count;
    case 3: return d->lba0;
    case 4: return d->lba1;
    case 5: return d->lba2;
    case 6: return d->head;
    case 7: return d->status;
    default: return 0xFF;
    }
}

static void dev_write_reg(IDE *d, int r, uint8_t v)
{
#ifdef IDE_TRACE
    if ((r & 7) != 7) fprintf(stderr, "  W r%d=%02X\n", r & 7, v);
#endif
    switch (r & 7) {
    case 1: d->features = v; break;
    case 2: d->count = v; break;
    case 3: d->lba0 = v; break;
    case 4: d->lba1 = v; break;
    case 5: d->lba2 = v; break;
    case 6: d->head = v; break;
    case 7: command(d, v); break;
    default: break;
    }
}

static void dev_write_control(IDE *d, uint8_t v)        /* 장치 제어: 비트 2 소프트 리셋 */
{
    if ((v & 0x04) && !(d->devctl & 0x04)) dev_reset(d);
    d->devctl = v;
}

static uint16_t dev_read_data(IDE *d)
{
#ifdef IDE_TRACE
    { static int nrd; nrd++; if (d->pos == 0) fprintf(stderr, "  data read start (xfer %d)\n", d->xfer); (void)nrd; }
#endif
    if ((d->xfer != 1 && d->xfer != 3) || !(d->status & ST_DRQ)) return 0xFFFF;
    uint16_t v = (uint16_t)(d->buf[d->pos] | (d->buf[d->pos + 1] << 8));
    d->pos += 2;
    if (d->pos >= 512 && d->xfer == 3) { d->xfer = 0; d->status = ST_DRDY | ST_DSC; return v; }
    if (d->pos >= 512) {
        d->status = ST_DRDY | ST_DSC;
        if (d->left > 1) {                              /* 다음 섹터 */
            d->left--;
            set_lba(d, cur_lba(d) + 1);
            d->count = (uint8_t)d->left;
            load(d);
        } else { d->left = 0; d->xfer = 0; d->count = 0; set_lba(d, cur_lba(d) + 1); }
    }
    return v;
}

static void dev_write_data(IDE *d, uint16_t v)
{
    if (d->xfer != 2 || !(d->status & ST_DRQ)) return;
    d->buf[d->pos] = (uint8_t)v; d->buf[d->pos + 1] = (uint8_t)(v >> 8);
    d->pos += 2;
    if (d->pos >= 512) {
        uint32_t lba = cur_lba(d);
        if (!d->wr || d->wr(d->ctx, lba, d->buf)) { fail(d, 0x10); return; }
        d->dirty = 1;
        d->pos = 0;
        set_lba(d, lba + 1);
        if (d->left > 1) { d->left--; d->count = (uint8_t)d->left; d->status = ST_DRDY | ST_DSC | ST_DRQ; }
        else { d->left = 0; d->xfer = 0; d->count = 0; d->status = ST_DRDY | ST_DSC; }
    }
}

/* 3.4.1: 8비트 자료 포트 — 두 번에 한 낱말 */
uint8_t ide_read_data8(IDE *d)
{
    if (d->half) { d->half = 0; return d->latch; }
    uint16_t w = ide_read_data(d);
    d->latch = (uint8_t)(w >> 8); d->half = 1;
    return (uint8_t)w;
}

void ide_write_data8(IDE *d, uint8_t v)
{
    if (!d->half) { d->latch = v; d->half = 1; return; }
    d->half = 0;
    ide_write_data(d, (uint16_t)(d->latch | (v << 8)));
}

/* ---- 3.4.3 DEV-15: 마스터·슬레이브 (공용 레지스터는 둘 다에 쓰고, 읽기·명령·자료는 DEV 비트로 고른 장치) ---- */
static IDE *sel(IDE *m)                                 /* 0 = 고른 장치가 없음 (슬레이브 없음) */
{
    if (!(m->head & 0x10)) return m;
    return (m->slave && m->slave->present) ? m->slave : 0;
}

void ide_reset(IDE *d)
{
    dev_reset(d);
    if (d->slave) dev_reset(d->slave);
}

uint8_t ide_read_reg(IDE *d, int r)
{
    IDE *s = sel(d);
    if (!s) return (r & 7) == 6 ? d->head : 0x7F;      /* 슬레이브 없음: 모든 레지스터 7Fh (BSY 꺼짐, openMSX와 같음) */
    return dev_read_reg(s, r);
}

void ide_write_reg(IDE *d, int r, uint8_t v)
{
    if ((r & 7) == 7) { IDE *s = sel(d); if (s) dev_write_reg(s, r, v); return; }
    dev_write_reg(d, r, v);
    if (d->slave) dev_write_reg(d->slave, r, v);
}

void ide_write_control(IDE *d, uint8_t v)
{
    dev_write_control(d, v);
    if (d->slave) dev_write_control(d->slave, v);
}

uint16_t ide_read_data(IDE *d)
{
    IDE *s = sel(d);
    return s ? dev_read_data(s) : 0x7F7F;
}

void ide_write_data(IDE *d, uint16_t v)
{
    IDE *s = sel(d);
    if (s) dev_write_data(s, v);
}
