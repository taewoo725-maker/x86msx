/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* bootinfo.h - 로더가 남기는 정보 (boot/loader.asm과 일치) */
#ifndef BOOTINFO_H
#define BOOTINFO_H
#include <stdint.h>

typedef struct {
    uint32_t magic;             /* 'BOOT' */
    uint32_t lfb;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t mode;
    uint32_t drive;
    uint32_t mem_end;           /* 3.0: 1 MiB 위 연속 메모리 끝 주소 (0 = 모름, INT 15h E801) */
    uint32_t bpp;               /* 8 또는 32 */
    uint32_t red_pos;           /* 32비트일 때 빨강 비트 위치 (16이면 B,G,R,x) */
} BootInfo;

typedef struct {
    uint32_t lba, bytes, load, reserved;
} PayloadItem;

typedef struct {
    uint32_t magic;             /* 'MSXP' */
    uint32_t count;             /* 항목 수 (0 = 본체, 1~ = ROM) */
    uint32_t reserved[2];
    PayloadItem item[8];
} PayloadHeader;

#define BOOTINFO_ADDR 0x0500
#define HEADER_ADDR   0x0600
#define BOOT_MAGIC    0x544F4F42u   /* 'BOOT' */
#define PAYLOAD_MAGIC 0x5058534Du   /* 'MSXP' */

#endif
