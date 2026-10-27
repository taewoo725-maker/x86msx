/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* bios.h - 레거시: 보호 모드에서 실모드 BIOS 인터럽트 호출 (boot/thunk.asm) */
#ifndef MSX_BIOS_H
#define MSX_BIOS_H
#include <stdint.h>

typedef struct __attribute__((packed)) {
    uint32_t eax, ebx, ecx, edx, esi, edi;
    uint16_t ds, es;
    uint32_t flags;
    uint8_t intno, pic_m, pic_s;
} BiosRegs;

#define BIOS_BOUNCE      0x10000u   /* 디스크 읽기 버퍼 (64 KiB, 1 MiB 아래) */
#define BIOS_BOUNCE_SECT 64

void bios_init(uint8_t pic_m, uint8_t pic_s);       /* 부팅 때 BIOS의 PIC 마스크 */
void bios_call(BiosRegs *r);
/* 드라이브(0x80~)에서 LBA부터 count(<=64)섹터를 BIOS_BOUNCE로. 성공 0 */
int  bios_read(int drive, uint64_t lba, int count);
int  bios_has_ext(int drive);
/* BIOS_BOUNCE의 count(<=64)섹터를 드라이브의 LBA에 쓰기. 성공 0 */
int  bios_write(int drive, uint64_t lba, int count);
/* 드라이브 전체 섹터 수 (INT 13h AH=48h). 실패 0 */
uint64_t bios_disk_sectors(int drive);
#endif
