/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* hdfat.h - 8.0.110 하드 디스크·플로피 이미지(섹터를 읽는 FAT12/16/32 볼륨) 안의 파일을 읽기 전용으로 찾고 읽기.
 * X86 Multimedia 영상이 MSX 하드 이미지의 MP4·MV 를 직접 읽을 때 쓴다. 플랫폼과 무관 (tests/hdfattest.c). */
#ifndef X86MSX_HDFAT_H
#define X86MSX_HDFAT_H
#include <stdint.h>

#define HF_CK 4096                       /* 클러스터 사슬 이정표 수 (큰 파일 건너뛰기용) */
#define HF_CKSTEP 256                    /* 이정표 간격 (클러스터 수) */
typedef int (*HfRead)(void *ctx, uint32_t lba, uint8_t *buf512);   /* 성공 0 */
typedef struct {
    HfRead rd; void *ctx;
    uint32_t start, nsec;                /* 볼륨 시작 섹터(절대)·섹터 수 */
    int fat_bits;                        /* 12 / 16 / 32 */
    uint32_t spc, fat_lba, fat_secs, root_lba, root_ents, data_lba, clusters, root_clus;
    /* 열린 파일 */
    uint32_t f_first, f_size;
    uint32_t cur_idx, cur_clus;          /* 마지막 읽은 사슬 위치 */
    uint32_t ck[HF_CK];                  /* ck[i] = 사슬 번호 i*HF_CKSTEP 의 클러스터 (0 = 아직 모름) */
    uint8_t fsec[512]; uint32_t fsec_lba;
} HdFat;

/* 볼륨 해석 (BPB). 성공 0 */
int hf_open(HdFat *f, HfRead rd, void *ctx, uint32_t start, uint32_t nsec);            /* FAT12·FAT16·FAT32 (8.0.114 마스터 결정: 막지 않고 다 읽음) */
/* "DIR\NAME.EXT" (드라이브 글자는 앞에서 걸러짐) → 파일을 열어 둠. 찾으면 1 (크기 *size), 아니면 0. 짧은 이름과 긴 이름(대소문자 무시) 모두 */
int hf_find(HdFat *f, const char *path, uint32_t *size);
/* 열어 둔 파일 읽기. 성공 0 */
int hf_read(HdFat *f, uint32_t off, uint8_t *buf, uint32_t len);
/* 파티션 표(MBR) 의 n 번째(0부터) FAT 파티션 → 시작·섹터 수. 표가 없고 0 번 섹터가 FAT 부트 섹터면 n=0 은 볼륨 전체. 성공 1 */
int hf_part(HfRead rd, void *ctx, int n, uint32_t *start, uint32_t *nsec);
#endif
