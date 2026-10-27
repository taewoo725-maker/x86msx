/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fw.h - 3.4.0 기종 내장 펌웨어·장치 (DEV-21·23·24)
 *   FW_PANASONIC  파나소닉 펌웨어 매퍼 (blueMSX 종류 27 16 KB SRAM, 68 32 KB, 142 WX 16 KB): 8 KiB 창 8개(0x0000~0xFFFF),
 *                 0x6000~0x7FEF 쓰기 = 창 뱅크 아래 8비트(창 5·6은 서로 바뀜), 0x7FF8 = 9번째 비트, 0x7FF9 = 제어.
 *                 뱅크 0x80~ = SRAM, 0x180~ = 본체 매퍼 RAM. 제어 비트 2·4·3이면 0x7FF0~7·0x7FF8·0x7FF9 읽기로 상태
 *   FW_NATIONAL   내셔널 펌웨어 (종류 31): 16 KiB 창 4개, 0x6400/0x6000/0x7000/0x7400 = 창 0/1/2/3, 0x7FF9 제어,
 *                 제어 비트 1이면 x3FFA~C = SRAM 주소, x3FFD = SRAM 자료(4 KB, 읽고 쓰면 주소 +1), 비트 2면 0x7FF0/2/4/6 = 뱅크
 *   FW_HALNOTE    소니 HB-F1XDJ Halnote (종류 40, 롬 1 MB): 0x4000~0xBFFF 8 KiB 뱅크(x0FFF에 쓰기), 창 2 비트 7 = SRAM 16 KB를
 *                 0x0000~0x3FFF에, 창 3 비트 7 = 0x7000~0x7FFF에 2 KB 작은 뱅크 둘(0x77FF·0x7FFF, 롬 0x80000부터)
 *   Bunsetsu/Jisyo (종류 28·29): 문절 변환 롬 0x4000~0xBFFF, 0xBFFC~E = 사전 주소(17비트), 0xBFFF 읽기 = 사전 바이트(주소 +1)
 *   Matsushita    (종류 26): 교환 I/O 장치 ID 8 (포트 40h에 8을 쓰면 41h~4Fh): 41h 펌웨어 스위치·터보, 43h·44h 무늬, 47h·48h 주소,
 *                 49h SRAM 2 KB 자료(주소 +1)
 * 동작은 openMSX(RomPanasonic·RomNational·RomHalnote·MSXBunsetsu·MSXMatsushita)를 보고 X86MSX 구조로 새로 작성 */
#ifndef MSX_FW_H
#define MSX_FW_H
#include <stdint.h>

enum { FW_NONE, FW_PANASONIC, FW_NATIONAL, FW_HALNOTE };

typedef struct {
    int kind;
    const uint8_t *rom;
    uint32_t rom_size;
    int ps, ss;                         /* -1 = 없음 */
    int start, count;                   /* 차지하는 8 KiB 블록 */
    uint16_t bank[8];
    uint8_t ctrl;
    uint8_t sub[2];                     /* Halnote 작은 뱅크 */
    uint8_t sram_on, sub_on;            /* Halnote */
    uint32_t sram_addr;                 /* National */
    uint32_t sram_size;
    uint8_t sram_mirror, ram_banks;     /* 파나소닉: SRAM을 뱅크 8개에 되풀이(WX·WSX), 0x180~ = 본체 RAM(turbo R) */
    uint8_t sram_dirty;
    uint8_t sram[32768];
    /* Bunsetsu */
    const uint8_t *jisyo;
    uint32_t jisyo_size, jisyo_addr;
    int bun_ps, bun_ss;                 /* -1 = 없음 */
    /* Matsushita (교환 I/O ID 8) */
    uint8_t mat_on, mat_switch, mat_turbo, mat_pattern, mat_c1, mat_c2;
    uint16_t mat_addr;
    uint8_t mat_dirty;
    uint8_t mat_sram[2048];
} FwDev;

#endif
