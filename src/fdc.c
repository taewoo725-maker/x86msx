/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * fdc.c - WD2793 / MB8877A 계열 (설계서 §6.7)
 *
 * 명령: 0x0X Restore, 0x1X Seek, 0x2X~0x7X Step / Step-in / Step-out,
 *       0x8X/0x9X 섹터 읽기, 0xAX/0xBX 섹터 쓰기, 0xCX 주소 읽기, 0xEX 트랙 읽기, 0xFX 트랙 쓰기,
 *       0xDX 강제 인터럽트.
 * 회전은 300 rpm(200 ms)으로 보고 처음 4 ms 동안 INDEX 비트를 켠다.
 */
#include "fdc.h"
#include <string.h>

#define T_PER_REV   715909u         /* 3.579545 MHz × 0.2 s */
#define T_INDEX     14318u          /* 4 ms */

static FdcDrive *cur(FDC *f) { return &f->drv[f->drive == 0 || (f->drive == 1 && f->ndrives != 1) ? f->drive : 2]; }   /* 3.2.0: 없는 드라이브 = 2 */

void fdc_reset(FDC *f)
{
    FdcDrive d0 = f->drv[0], d1 = f->drv[1];
    int nd = f->ndrives;
    memset(f, 0, sizeof *f);
    f->ndrives = nd;
    f->last_access[0] = f->last_access[1] = 0;
    f->drv[0] = d0; f->drv[1] = d1;                     /* drv[2]는 비어 있음 */
    f->drv[0].head = f->drv[1].head = 0;
    f->step_dir = 1;
    f->sector = 1;
}

void fdc_insert(FDC *f, int d, uint8_t *data, uint32_t size, int wp)
{
    FdcDrive *v = &f->drv[d & 1];
    v->data = data; v->size = size; v->wp = wp; v->dirty = 0;
    v->tracks = 80; v->sides = 2; v->spt = 9;
    switch (size) {
    case 184320: v->tracks = 40; v->sides = 1; v->spt = 9; break;
    case 163840: v->tracks = 40; v->sides = 1; v->spt = 8; break;
    case 327680: v->tracks = 80; v->sides = 1; v->spt = 8; break;
    case 368640: v->tracks = 80; v->sides = 1; v->spt = 9; break;
    case 655360: v->tracks = 80; v->sides = 2; v->spt = 8; break;
    case 1474560: v->tracks = 80; v->sides = 2; v->spt = 18; break;
    default: v->tracks = (int)(size / (512u * 2 * 9)); if (v->tracks < 1) v->tracks = 1; break;
    }
    /* 3.0.2: 부트 섹터의 BPB가 맞으면 그것을 따름 (같은 크기의 5.25" 40트랙 2면과 3.5" 80트랙 1면을 나눔) */
    if (data && size >= 512) {
        uint32_t bps = data[11] | data[12] << 8, spt = data[24] | data[25] << 8, heads = data[26] | data[27] << 8, tot = data[19] | data[20] << 8;
        if (bps == 512 && spt >= 8 && spt <= 18 && heads >= 1 && heads <= 2 && tot * 512 == size && tot % (spt * heads) == 0 && data[21] >= 0xF0) {
            v->spt = (int)spt; v->sides = (int)heads; v->tracks = (int)(tot / (spt * heads));
        }
    }
}

void fdc_eject(FDC *f, int d) { f->drv[d & 1].data = 0; f->drv[d & 1].size = 0; }

static int index_on(FDC *f)
{
    FdcDrive *v = cur(f);
    return v->data && f->motor && (f->now % T_PER_REV) < T_INDEX;
}

/* 섹터 위치 (바이트). 없으면 -1 */
static long sector_off(FDC *f, int sec)
{
    FdcDrive *v = cur(f);
    if (!v->data || sec < 1 || sec > v->spt || v->head >= v->tracks) return -1;
    if (f->side >= v->sides) return -1;
    long lba = ((long)v->head * v->sides + f->side) * v->spt + (sec - 1);
    long off = lba * 512;
    return off + 512 <= (long)v->size ? off : -1;
}

static void end_cmd(FDC *f)
{
    f->status &= (uint8_t)~0x03;        /* BUSY, DRQ 해제 */
    f->drq = 0;
    f->irq = 1;
    f->xfer = 0;
}

static uint8_t type1_status(FDC *f)
{
    FdcDrive *v = cur(f);
    uint8_t s = (uint8_t)(f->status & 0x18);            /* 탐색 오류, CRC */
    if (!v->data) s |= 0x80;
    if (v->data && v->wp) s |= 0x40;
    s |= 0x20;                                           /* 헤드 내려옴 */
    if (v->head == 0 && v != &f->drv[2]) s |= 0x04;      /* 3.2.0: 없는 드라이브는 트랙 0 신호가 오지 않음 */
    if (index_on(f)) s |= 0x02;
    return s;
}

static void touch(FDC *f) { if (f->drive >= 0 && f->drive <= 1) f->last_access[f->drive] = f->now; }

static void sector_wait(FDC *f, int first) { (void)first; f->drq = 1; f->status |= 0x02; }   /* 섹터 바로 준비 (3.6.6: 디스크 시간 흉내 없앰) */
void fdc_poll(FDC *f, uint64_t now)
{
    if (f->ready_at && now >= f->ready_at && f->xfer) { f->ready_at = 0; f->drq = 1; f->status |= 0x02; }
    if (!f->xfer) f->ready_at = 0;
}

static int load_sector_first(FDC *f, int first)
{
    touch(f);
    long off = sector_off(f, f->sector);
    if (off < 0) { f->status = (uint8_t)((f->status & ~0x01) | 0x10); end_cmd(f); return 0; }    /* RNF */
    memcpy(f->buf, cur(f)->data + off, 512);
    f->pos = 0; f->len = 512;
    sector_wait(f, first);
    return 1;
}
static int load_sector(FDC *f) { return load_sector_first(f, 0); }

static void track_write_commit(FDC *f)
{
    /* 포맷: 바이트 흐름에서 ID 표시(FE) 뒤 C H R N, 데이터 표시(FB) 뒤 512바이트를 찾아 섹터에 쓴다 */
    FdcDrive *v = cur(f);
    for (int i = 0; i + 5 < f->len; i++) {
        if (f->buf[i] != 0xFE) continue;
        int r = f->buf[i + 3];
        for (int j = i + 5; j < i + 60 && j + 512 < f->len; j++) {
            if (f->buf[j] != 0xFB) continue;
            long off = sector_off(f, r);
            if (off >= 0) { memcpy(v->data + off, f->buf + j + 1, 512); fdc_mark(v, (uint32_t)off, 512); }
            i = j + 512;
            break;
        }
    }
}

static void command(FDC *f, uint8_t c)
{
    FdcDrive *v = cur(f);
    f->cmd = c;
    f->irq = 0;
    if ((c & 0xF0) == 0xD0) {                            /* 강제 인터럽트 */
        f->xfer = 0; f->drq = 0;
        f->status = type1_status(f);
        f->busy_type = 1;
        if (c & 0x08) f->irq = 1;
        return;
    }
    if (!(c & 0x80)) {                                   /* 종류 I */
        f->busy_type = 1;
        f->status = 0;
        int op = c >> 4;
        if (op == 0) { v->head = 0; f->track = 0; }
        else if (op == 1) {
            int delta = (int)f->data - (int)f->track;
            v->head += delta;
            if (v->head < 0) v->head = 0;
            if (v->head > 85) v->head = 85;
            f->track = f->data;
        } else {
            if (op >= 4 && op <= 5) f->step_dir = 1;
            if (op >= 6) f->step_dir = -1;
            v->head += f->step_dir;
            if (v->head < 0) v->head = 0;
            if (v->head > 85) v->head = 85;
            if (c & 0x10) f->track = (uint8_t)(f->track + f->step_dir);
        }
        if ((c & 0x04) && v->data && v->head != f->track) f->status |= 0x10;   /* 확인 실패 = 탐색 오류 */
        if (op == 0 && v == &f->drv[2]) f->status |= 0x10;                     /* 3.2.0: 없는 드라이브는 트랙 0을 못 찾음 */
        f->status = (uint8_t)(type1_status(f) | (f->status & 0x10));
        f->irq = 1;
        return;
    }
    f->busy_type = (c & 0x40) ? 3 : 2;
    f->status = 0x01;                                    /* BUSY */
    if (!v->data) { f->status = 0x80; f->irq = 1; return; }
    switch (c & 0xF0) {
    case 0x80: case 0x90:                                /* 섹터 읽기 */
        f->multi = (c & 0x10) != 0;
        f->xfer = 1;
        load_sector_first(f, 1);
        break;
    case 0xA0: case 0xB0:                                /* 섹터 쓰기 */
        if (v->wp) { f->status = 0x40; f->irq = 1; return; }
        if (sector_off(f, f->sector) < 0) { f->status = 0x10; f->irq = 1; return; }
        f->multi = (c & 0x10) != 0;
        f->xfer = 2; f->pos = 0; f->len = 512;
        sector_wait(f, 1);
        break;
    case 0xC0:                                           /* 주소 읽기 */
        f->buf[0] = (uint8_t)v->head; f->buf[1] = (uint8_t)f->side; f->buf[2] = 1; f->buf[3] = 2; f->buf[4] = f->buf[5] = 0;
        f->sector = (uint8_t)v->head;
        f->xfer = 3; f->pos = 0; f->len = 6;
        f->drq = 1; f->status |= 0x02;
        break;
    case 0xE0:                                           /* 트랙 읽기: 지원 안 함 (빈 완료) */
        end_cmd(f);
        break;
    case 0xF0:                                           /* 트랙 쓰기 (포맷) */
        if (v->wp) { f->status = 0x40; f->irq = 1; return; }
        f->xfer = 4; f->pos = 0; f->len = 0;
        f->drq = 1; f->status |= 0x02;
        break;
    }
}

uint8_t fdc_read(FDC *f, int reg, uint64_t now)
{
    f->now = now;
    fdc_poll(f, now);
    switch (reg & 3) {
    case 0:
        f->irq = 0;
        if (f->busy_type == 1) return (uint8_t)(type1_status(f) | (f->status & 0x10));
        {
            FdcDrive *v = cur(f);
            uint8_t s = f->status;
            if (!v->data) s |= 0x80;
            if (f->drq) s |= 0x02;
            return s;
        }
    case 1: return f->track;
    case 2: return f->sector;
    default:
        if ((f->xfer == 1 || f->xfer == 3) && f->ready_at) return f->data;   /* 3.3.1: 아직 준비 안 됨 */
        if (f->xfer == 1 || f->xfer == 3) {
            f->data = f->buf[f->pos++];
            if (f->pos >= f->len) {
                if (f->xfer == 1 && f->multi) {
                    f->sector++;
                    if (sector_off(f, f->sector) >= 0) { load_sector(f); return f->data; }
                }
                f->status &= (uint8_t)~0x10;
                end_cmd(f);
            }
        }
        return f->data;
    }
}

void fdc_write(FDC *f, int reg, uint8_t v, uint64_t now)
{
    f->now = now;
    fdc_poll(f, now);
    switch (reg & 3) {
    case 0: command(f, v); break;
    case 1: if (!(f->status & 0x01) || f->busy_type == 1) f->track = v; break;
    case 2: if (!(f->status & 0x01) || f->busy_type == 1) f->sector = v; break;
    default:
        f->data = v;
        if (f->xfer) touch(f);
        if (f->xfer == 2 && f->ready_at) break;                 /* 3.3.1: 아직 준비 안 됨 */
        if (f->xfer == 2) {
            f->buf[f->pos++] = v;
            if (f->pos >= 512) {
                long off = sector_off(f, f->sector);
                if (off >= 0) { memcpy(cur(f)->data + off, f->buf, 512); fdc_mark(cur(f), (uint32_t)off, 512); }
                if (f->multi && sector_off(f, f->sector + 1) >= 0) { f->sector++; f->pos = 0; sector_wait(f, 0); }
                else end_cmd(f);
            }
        } else if (f->xfer == 4) {
            if (f->len < (int)sizeof f->buf) f->buf[f->len++] = v;
            if (f->len >= 6250) { track_write_commit(f); end_cmd(f); }
        }
        break;
    }
}
