/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* bios.c - 실모드 BIOS 호출 (INT 13h 디스크) */
#include "bios.h"
#include <string.h>

extern const uint8_t thunk_bin[], thunk_bin_end[];
#define THUNK_ADDR 0x9000u
#define REGS_ADDR  0x8E00u
#define DAP_ADDR   0x8F00u

static uint8_t bios_pic_m, bios_pic_s;

void bios_init(uint8_t pic_m, uint8_t pic_s)
{
    bios_pic_m = pic_m;
    bios_pic_s = pic_s;
    memcpy((void *)THUNK_ADDR, thunk_bin, (size_t)(thunk_bin_end - thunk_bin));
}

void bios_call(BiosRegs *r)
{
    r->pic_m = bios_pic_m | 0x02;      /* 2.10.1: BIOS 호출 동안 IRQ1(키보드)은 막음 — BIOS INT 9가 키 바이트를 가져가면
                                        * 뗌 신호를 잃어 MSX 키가 눌린 채로 남는다. 바이트는 8042에 남았다가 kbc_drain이 읽음 */
    r->pic_s = bios_pic_s;
    memcpy((void *)REGS_ADDR, r, sizeof *r);
    ((void (*)(void))THUNK_ADDR)();
    memcpy(r, (void *)REGS_ADDR, sizeof *r);
}

int bios_has_ext(int drive)
{
    BiosRegs r;
    memset(&r, 0, sizeof r);
    r.intno = 0x13;
    r.eax = 0x4100;
    r.ebx = 0x55AA;
    r.edx = (uint32_t)drive;
    bios_call(&r);
    return !(r.flags & 1) && (r.ebx & 0xFFFF) == 0xAA55 && (r.ecx & 1);
}

static int disk_io(int ah, int drive, uint64_t lba, int count);

int bios_read(int drive, uint64_t lba, int count) { return disk_io(0x42, drive, lba, count); }
int bios_write(int drive, uint64_t lba, int count) { return disk_io(0x43, drive, lba, count); }

uint64_t bios_disk_sectors(int drive)
{
    volatile uint8_t *b = (volatile uint8_t *)(DAP_ADDR + 0x40);
    for (int i = 0; i < 0x1E; i++) b[i] = 0;
    b[0] = 0x1A;                                        /* 버퍼 크기 */
    BiosRegs r;
    memset(&r, 0, sizeof r);
    r.intno = 0x13;
    r.eax = 0x4800;
    r.edx = (uint32_t)drive;
    r.esi = DAP_ADDR + 0x40;
    bios_call(&r);
    if (r.flags & 1) return 0;
    uint64_t n = 0;
    for (int i = 0; i < 8; i++) n |= (uint64_t)b[0x10 + i] << (8 * i);
    uint16_t bps = (uint16_t)(b[0x18] | (b[0x19] << 8));
    return (bps == 512 || bps == 0) ? n : 0;
}

static int disk_io(int ah, int drive, uint64_t lba, int count)
{
    if (count < 1 || count > BIOS_BOUNCE_SECT) return -1;
    for (int tries = 0; tries < 3; tries++) {
        volatile uint8_t *dap = (volatile uint8_t *)DAP_ADDR;
        dap[0] = 0x10; dap[1] = 0;
        dap[2] = (uint8_t)count; dap[3] = 0;
        dap[4] = 0; dap[5] = 0;                                     /* 오프셋 0 */
        dap[6] = (uint8_t)(BIOS_BOUNCE >> 4); dap[7] = (uint8_t)(BIOS_BOUNCE >> 12);   /* 세그먼트 0x1000 */
        for (int i = 0; i < 8; i++) dap[8 + i] = (uint8_t)(lba >> (8 * i));
        BiosRegs r;
        memset(&r, 0, sizeof r);
        r.intno = 0x13;
        r.eax = (uint32_t)ah << 8;                         /* 42h 읽기, 43h 쓰기 (AL=0: 검증 없음) */
        r.edx = (uint32_t)drive;
        r.esi = DAP_ADDR;
        bios_call(&r);
        if (!(r.flags & 1)) return 0;
        BiosRegs rs;                                                /* 실패: 드라이브 리셋 후 다시 */
        memset(&rs, 0, sizeof rs);
        rs.intno = 0x13;
        rs.edx = (uint32_t)drive;
        bios_call(&rs);
    }
    return -1;
}
