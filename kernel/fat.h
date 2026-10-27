/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * fat.h - FAT16/FAT32 읽기·쓰기 (파일 만들기·덮어쓰기·폴더 만들기, 긴 이름)
 * 섹터 읽기 함수를 받아 쓰므로 호스트에서 이미지 파일로 시험할 수 있다 (tests/fattest.c).
 */
#ifndef MSX_FAT_H
#define MSX_FAT_H
#include <stdint.h>

/* lba부터 count(<=64)섹터를 buf로. 성공 0 */
typedef int (*FatRead)(void *ctx, uint64_t lba, uint32_t count, uint8_t *buf);
typedef int (*FatWrite)(void *ctx, uint64_t lba, uint32_t count, const uint8_t *buf);

typedef struct {
    FatRead read;
    FatWrite write;                 /* 없으면 NULL (읽기 전용) */
    void *ctx;
    uint32_t part_lba;
    int fat32;
    uint32_t spc, fat_lba, data_lba, root_lba, root_secs, root_clus, clusters;
    uint32_t nfats, fatsz;          /* FAT 사본 수, FAT 하나의 섹터 수 */
    uint32_t fsinfo_lba;            /* FAT32 FSInfo 섹터 (0 = 없음) */
    int fsinfo_done;                /* 빈 클러스터 수를 '모름'으로 바꿨음 */
    uint64_t bytes;
    char label[12];
    uint32_t cache_lba;
    uint8_t cache[512];
} FatVol;

typedef struct {
    char key[768];              /* UTF-8 이름 (경로에 씀). 3.4.7: 긴 이름 255자까지 */
    char disp[64];              /* 표시용 (ASCII 밖은 '?') */
    int is_dir;
    uint32_t size, clus;
    uint16_t year;              /* 0 = 모름 */
    uint8_t mon, day, hour, min;
    uint32_t ent_lba;           /* 디렉터리 항목이 있는 섹터 (크기 바꾸기 쓰기용) */
    uint16_t ent_off;           /* 섹터 안 위치 */
} FatEntry;

/* 디스크(LBA 0부터)에서 FAT 볼륨 찾기: MBR 파티션 또는 파티션 없는 디스크. 찾은 수 */
int fat_scan_disk(FatRead rd, void *ctx, FatVol *out, int max);
/* 3.7.0: lba의 부트 섹터 하나를 FAT16/32 볼륨으로 (FAT12는 실패). 성공 0 */
int fat_mount(FatVol *v, FatRead rd, void *ctx, uint32_t lba);
/* path: "" 또는 "\" = 최상위, "\ROMS\Konami" 등 (대소문자 무시). 항목 수, 실패 -1 */
int fat_list(FatVol *v, const char *path, FatEntry *out, int max);
int fat_list_skip(FatVol *v, const char *path, FatEntry *out, int max, int skip);   /* 4.0.29 */
/* 파일을 buf에 최대 max바이트. *size = 파일 크기. 성공 0 */
int fat_read_file(FatVol *v, const char *path, uint8_t *buf, uint32_t max, uint32_t *size);
/* 이미 있는 파일을 같은 자리에 덮어쓴다 (크기·클러스터 그대로, FAT 수정 없음).
 * len은 파일 크기와 같아야 한다. 성공 0 */
int fat_write_inplace(FatVol *v, const char *path, const uint8_t *data, uint32_t len);
/* 이미 있는 파일을 len바이트로 다시 쓴다. 크기가 늘면 빈 클러스터를 이어 붙이고 줄면 남는 것을 푼다.
 * FAT 사본 모두와 디렉터리 항목 크기를 고친다 (FAT16/32). 성공 0 */
int fat_write_file(FatVol *v, const char *path, const uint8_t *data, uint32_t len);
/* 파일이 차지한 디스크 영역을 이어진 구간으로 (하드 디스크 이미지를 섹터 단위로 읽고 쓰기, 2.8).
 * ext[i] = { 장치 LBA, 섹터 수 }. 구간 수, 실패 -1 (max를 넘어도 -1). *size = 파일 크기 */
/* 새 빈 파일(is_dir=0) 또는 폴더(1)를 만든다 (긴 이름 포함). 이미 있으면 0. 부모 폴더는 있어야 함. 성공 0 (2.9) */
int fat_create(FatVol *v, const char *path, int is_dir);
/* 만들기·쓰기 때 날짜 (FAT 형식 date·time). 없으면 고정 날짜 (2.10) */
void fat_set_clock(void (*fn)(uint16_t *date, uint16_t *time));
typedef struct { uint64_t lba; uint32_t secs; } FatExtent;
int fat_file_extents(FatVol *v, const char *path, FatExtent *ext, int max, uint32_t *size);
/* 3.0.2: 만든 빈 파일에 len 바이트 자리를 잡고 앞부분을 prefix로 (하드 디스크 이미지). 성공 0 */
int fat_alloc_file(FatVol *v, const char *path, uint32_t len, const uint8_t *prefix, uint32_t plen, void (*progress)(int pct));
/* 3.0.2: 파일 지우기 (폴더 안 됨). 성공 0 */
int fat_delete(FatVol *v, const char *path);
#endif
