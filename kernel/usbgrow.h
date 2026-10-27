/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * usbgrow.h - "USB 전체 용량 사용" (요구사항서 2.1, 설계서 §3.1)
 *
 * 작은 이미지를 큰 USB에 기록하면 FAT32 파티션이 이미지 크기(63 MiB)로 남는다.
 * 이 모듈은 첫 파티션의 파일을 모두 메모리로 읽고, 디스크 전체 크기의 FAT32를 새로 만든 뒤
 * 파일을 다시 쓰고, 마지막에 MBR 파티션 표를 고친다. 파티션 앞의 레거시 로더(LBA 1~2047)는 그대로.
 * 섹터 읽기·쓰기 함수만 받으므로 호스트에서 디스크 이미지 파일로 시험한다(tests/growtest.c).
 */
#ifndef MSX_USBGROW_H
#define MSX_USBGROW_H
#include <stdint.h>

typedef struct {
    int (*read)(void *ctx, uint64_t lba, uint32_t count, uint8_t *buf);          /* count <= 64 */
    int (*write)(void *ctx, uint64_t lba, uint32_t count, const uint8_t *buf);   /* count <= 64 */
    void *ctx;
    uint64_t disk_sectors;          /* 디스크 전체 섹터 수 (512바이트) */
    uint8_t *mem;                   /* 파일을 담을 메모리 (64 MiB 이상 권장) */
    uint64_t mem_size;
    void (*progress)(int percent, const char *what);
} GrowIO;

/* 현재 첫 파티션 크기(섹터). 실패 0 */
uint64_t usbgrow_part_sectors(GrowIO *io);

/* 실행. 0 성공(다시 시작 필요), 1 이미 전체 크기, -1 실패(err) */
int usbgrow_run(GrowIO *io, char *err, int errlen);
#endif
