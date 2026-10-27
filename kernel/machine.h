/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* machine.h - 기종 프로필(config.ini)을 읽어 기계 조립 (레거시·UEFI 공용) */
#ifndef MSXEMU_MACHINE_LOAD_H
#define MSXEMU_MACHINE_LOAD_H
#include "../src/msx.h"

typedef int (*MachineRead)(const char *path, uint8_t *buf, uint32_t max, uint32_t *size);

#define DEFAULT_MACHINE "MSX - Daewoo DPC-200"
#define FALLBACK_MACHINE "MSX - C-BIOS"      /* 4.0.28: 처음 기종(DPC-200)에 BIOS가 없으면 이것으로 켬 (이미지에 들어 있는 무료 BIOS) */

/* "0:\Machines\<name>\config.ini"을 읽고 ROM을 찾아 msx_setup. 성공 0, 실패 시 err에 이유 */
int machine_load(MSX *m, MachineRead rd, const char *name, MachineProfile *out, char *err, int errlen);
/* 켤 수 있는지 검사: 0 가능, 1 BIOS 없음(miss = 파일 경로), 2 미지원 VDP(miss = VDP 이름), 3 config.ini 없음 */
int machine_check(MachineRead rd, const char *name, char *miss, int misslen);
/* 4.0.28: 처음 켤 기종 이름. DEFAULT_MACHINE을 켤 수 있으면 그것, 아니면 FALLBACK_MACHINE(C-BIOS)을 켤 수 있을 때 그것, 둘 다 안 되면 DEFAULT_MACHINE */
const char *machine_default_name(MachineRead rd);
/* 3.3.2 ACC-04: 롬 파일이 없을 때 이름으로 다른 파일 찾기(SHA-1 기억), 읽은 롬 알리기. NULL이면 안 씀 */
extern uint8_t *machine_big;            /* 3.4.0 큰 펌웨어 롬 자리 (NULL이면 1 MB 롬 자리에) */
extern uint32_t machine_big_size;
extern uint8_t *cart_big;               /* 4.0.129: 큰 카트리지 롬 자리 (플랫폼이 정함) */
extern uint32_t cart_big_size;
extern int (*machine_rom_find)(const char *base, uint8_t *buf, uint32_t max, uint32_t *size);
extern void (*machine_rom_seen)(const char *base, const uint8_t *data, uint32_t size);
#endif
