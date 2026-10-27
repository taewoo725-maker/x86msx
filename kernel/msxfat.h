/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* msxfat.h - 3.0.2 메모리에 올린 MSX 디스크 이미지(.dsk, FAT12) 안의 파일 읽기·쓰기·지우기
 *
 * 디스크 파일 복사 창(fe_sys.inc)이 쓴다. 플랫폼과 무관 (tests/msxfattest.c).
 * BPB가 없는 옛 MSX-DOS 1 디스크는 FAT 첫 바이트(매체 바이트)와 이미지 크기로 형식을 정한다.
 * 폴더(MSX-DOS 2)는 들어가 읽고 쓸 수 있고, 루트·폴더 항목이 모자라면 폴더는 클러스터를 늘린다.
 */
#ifndef X86MSX_MSXFAT_H
#define X86MSX_MSXFAT_H
#include <stdint.h>

typedef struct {
    uint8_t *img;
    uint32_t size;
    uint32_t spc, nfats, fatsz, fat_off, root_off, root_ents, data_off, clusters;
    uint8_t media;
} MsxFat;

typedef struct {
    char name[13];              /* "NAME.EXT" (표시·USB 이름, ASCII 밖은 '_') */
    uint8_t raw[11];            /* 디렉터리의 이름 그대로 */
    uint8_t attr, is_dir;
    uint32_t size;
    uint16_t clus, date, time;
    uint32_t off;               /* 이미지 안 디렉터리 항목 위치 (바이트) */
} MfEntry;

enum { MF_OK = 0, MF_EBAD = -1, MF_ENOSPACE = -2, MF_EDIRFULL = -3, MF_EFAT16 = -4, MF_EIO = -5 };

/* 이미지 해석. 성공 0 */
int mf_open(MsxFat *f, uint8_t *img, uint32_t size);
/* 폴더 목록 (dclus 0 = 루트). 항목 수 */
int mf_list(MsxFat *f, uint16_t dclus, MfEntry *out, int max);
/* 파일 읽기: 읽은 바이트 수 (max까지) */
uint32_t mf_read(MsxFat *f, const MfEntry *e, uint8_t *buf, uint32_t max);
/* 파일 쓰기 (같은 이름이 있으면 바꿈). raw = 8+3 이름. 성공 0 */
int mf_write(MsxFat *f, uint16_t dclus, const uint8_t raw[11], const uint8_t *data, uint32_t len, uint16_t date, uint16_t time);
/* 3.7.3: 폴더 만들기 (같은 이름 폴더가 있으면 그것). *out = 폴더 클러스터. 성공 0 */
int mf_mkdir(MsxFat *f, uint16_t dclus, const uint8_t raw[11], uint16_t date, uint16_t time, uint16_t *out);
/* 파일 지우기 (폴더는 비어 있을 때만). 성공 0 */
int mf_delete(MsxFat *f, const MfEntry *e);
/* 이름 바꾸기 (3.7.0). 성공 0 */
int mf_rename(MsxFat *f, uint16_t dclus, const MfEntry *e, const uint8_t raw[11]);
/* 남은 바이트 */
uint32_t mf_free(MsxFat *f);
/* 긴 이름 → 8+3 (대문자, 못 쓰는 글자 '_'). 줄이거나 바꿨으면 1 */
int mf_make83(const char *name, uint8_t raw[11]);
#endif
