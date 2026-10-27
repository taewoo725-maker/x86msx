/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * flash.h - AMD 형 플래시 칩 공용 부품 (5.2.7 구조 개편 4단계, openMSX AmdFlash 처럼 칩 표로)
 *
 * 칩의 크기·섹터·명령 주소·ID/CFI 읽기는 칩 표(flash.c)가 정한다. 매퍼는 "칩 안 주소"로 flash_write 를 부르고,
 * 읽기 모드(자동 선택·CFI)면 flash_id_page 를 창에 놓는다. 롬 파일을 칩 크기까지 FFh 로 채우는 크기도 칩이 정함
 * (5.0.184 Go Figure: 프런트엔드가 크기를 따로 정해 생긴 버그를 구조로 막음).
 *
 * 상태(FlashChip)는 포인터가 없어 상태 저장에 그대로 들어간다. 칩 내용(롬 버퍼)과 바뀜 표시는 부를 때 FlashMem 으로 넘긴다.
 */
#ifndef MSX_FLASH_H
#define MSX_FLASH_H
#include <stdint.h>

typedef enum {
    FLASH_NONE = 0,
    FLASH_AM29F040,     /* 512 KiB, 섹터 64 KiB, 명령 555h/2AAh (11비트), ID 01/A4 — Mega Flash ROM SCC·Matra INK */
    FLASH_S29GL064N,    /* 8 MiB, 처음 64 KiB 는 8 KiB 섹터, 명령 AAAh/555h (+워드 모드 554h), 자동 선택·CFI(N형) — Yamanooto */
    FLASH_S29GL064S,    /* 같음 + CFI(S형), CFI 중 FFh 로도 되돌림 — ASCII16-X */
    FLASH_M29W128,      /* 16 MiB, 섹터 128 KiB 균일, 바이트 모드 명령 AAAh/555h, 자동 선택 20/7E/19/21/01 (x8: 짝·홀 같은 값) — popolon Flash-ROM SCC */
    FLASH_M29W800DB,    /* 1 MiB, 아래쪽 부트 블록 16K/8K/8K/32K + 64K x 15, 바이트 모드 명령 AAAh/555h, ID 20/5B — Mega Flash ROM SCC+ (CPU A12 가 칩 A0 에 이어짐: 명령 주소만 바꿔 봄) */
    FLASH_TYPES
} FlashType;

typedef struct {
    uint8_t type;       /* FlashType */
    uint8_t step;       /* 명령 단계 (0 = 대기) */
    uint8_t mode;       /* 0 = 읽기, 1 = 자동 선택(ID), 2 = CFI */
    uint8_t rsv;
} FlashChip;

typedef struct {
    uint8_t *data;      /* 칩 내용 (롬 버퍼) */
    uint32_t size;      /* 버퍼 크기 (칩 크기 이하일 수 있음: 그때는 크기로 감쌈) */
    uint8_t *dirty;     /* 바뀌면 1 (프런트엔드가 저장하고 지움) */
    uint8_t *dirty_map; /* 8 KiB 조각마다 바뀜 비트 (칩 표가 쓰라고 할 때만, 0 이면 안 씀) */
    uint32_t dirty_map_bytes;
} FlashMem;

void flash_init(FlashChip *f, FlashType type);          /* 타입 정하고 읽기 모드로 */
void flash_reset(FlashChip *f);                          /* 읽기 모드로 (명령 단계도 지움) */
int flash_write(FlashChip *f, const FlashMem *mem, uint32_t fa, uint8_t v);   /* fa = 칩 안 주소. 1 = 읽기 모드가 바뀜(창을 다시 맞출 것) */
const uint8_t *flash_id_page(const FlashChip *f);       /* 자동 선택·CFI 중 창에 보일 8 KiB (읽기 모드면 0) */
uint32_t flash_chip_size(FlashType type);               /* 칩 크기 (바이트) */
const char *flash_chip_name(FlashType type);

#endif
