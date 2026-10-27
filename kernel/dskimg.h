/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* dskimg.h - 3.0.2 빈 디스크·하드 디스크 이미지 만들기 (플랫폼과 무관, tests/dskimgtest.c)
 *
 * 디스크(.dsk): MSX-DOS가 쓰는 FAT12 형식 9가지 (매체 바이트 F0, F8~FF), 부트 섹터 + 빈 FAT + 빈 루트.
 * 하드 디스크(.dsk, IDE·SCSI용): MBR + 파티션 하나를 포맷한 앞부분(prefix)을 만들고, 나머지는 0으로 채운다.
 */
#ifndef X86MSX_DSKIMG_H
#define X86MSX_DSKIMG_H
#include <stdint.h>

typedef struct {
    const char *name;           /* 표시 이름 */
    uint8_t media;              /* 매체 바이트 */
    uint16_t sectors, spt, heads, tracks;
    uint8_t spc, fat_secs, nfats;
    uint16_t root_entries;
} DskFormat;

#define DSK_FORMATS 9
extern const DskFormat dsk_formats[DSK_FORMATS];

/* 디스크 이미지 전체를 out에 (크기 = sectors × 512). label: 볼륨 이름(11자까지, NULL 가능). 바이트 수 */
uint32_t dsk_build(int fmt, uint8_t *out, const char *label);
void dsk_boot_dos1(uint8_t *out);                      /* 3.6.1: MSXDOS.SYS를 읽어 부팅하는 부트 코드 (디렉토리 삽입) */

/* 하드 디스크 종류 */
enum { HDI_NEXTOR_FAT16, HDI_DOS2_FAT12, HDI_BLANK, HDI_KINDS };
/* 크기 목록 (MB). kind마다 쓸 수 있는 것만 hd_size_ok */
#define HD_SIZES 9
extern const uint16_t hd_sizes_mb[HD_SIZES];
int hd_size_ok(int kind, uint32_t mb);
/* 하드 디스크 이미지 앞부분(MBR, 부트 섹터, FAT, 루트)을 out에. 나머지는 모두 0이면 된다.
 * total_sectors = 이미지 전체 섹터 수. 앞부분 바이트 수 (0 = 이 크기는 못 만듦) */
uint32_t hd_build_prefix(int kind, uint32_t total_sectors, uint8_t *out, uint32_t max, const char *label);
#endif
