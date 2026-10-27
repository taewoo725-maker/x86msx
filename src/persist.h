/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * persist.h - 영속 데이터 선언 — 꺼도 남는 메모리(배터리 SRAM·플래시·펌웨어 SRAM)
 *
 * 장치(카트리지 매퍼·본체 펌웨어)가 "무엇을, 어떤 꼴로, 어떤 이름으로" 남길지 여기서 선언한다.
 * 프런트엔드는 이 선언만 보고 USB SRAM\ 폴더에 읽고 쓴다 (언제 쓸지 = 바뀐 뒤 2초·끝낼 때, 공용).
 * 새 장치 = msx_cart_persist() 의 표에 한 줄. 파일 이름·꼴은 예전 판과 같다 (사용자 저장 파일이 그대로 읽힘).
 */
#ifndef PERSIST_H
#define PERSIST_H
#include "msx.h"

typedef enum {
    PS_NONE,
    PS_RAW,             /* 메모리 그대로. 읽을 때 파일이 짧으면 앞부분만 */
    PS_PAC,             /* "PAC2 BACKUP DATA" 머리 16바이트 + 8190바이트 (FM-PAC·PAC). 머리가 없으면 그대로 읽음 */
    PS_FLASH,           /* 바뀐 8 KiB 조각만 ("XFL1"), 많으면 칩 전체 (ASCII16-X·Yamanooto) */
} PersistFmt;

typedef struct {
    uint8_t fmt;        /* PersistFmt */
    uint8_t from_rom;   /* 1 = 파일 이름 = 롬 파일 이름(확장자 뺌) + name. 0 = name 그대로 */
    char name[48];      /* 고정 이름 ("FMPAC.pac") 또는 롬 이름 뒤 확장자 (".sram") */
    uint8_t *data;      /* 남길 메모리 */
    uint32_t size;      /* 저장 길이 (PS_PAC 은 8190) */
    uint32_t load_max;  /* 읽을 때 최대 (머리 없는 PAC 은 8192) */
} MsxPersist;

/* 카트 자리 n 의 영속 데이터. has_rom = 일반 카트 자리(0~4)에 롬 파일을 넣었음, special = 특수 카트리지 메뉴로 넣음.
 * 돌려줌: 1 있음, 0 없음 */
int msx_cart_persist(MSX *m, int n, int has_rom, int special, MsxPersist *p);

/* 본체 펌웨어 SRAM (+ Matsushita 2 KiB): 파일 하나에 둘을 이어서. 돌려줌: 전체 길이 (0 = 없음) */
typedef struct { uint8_t *data[2]; uint32_t size[2]; uint8_t *dirty[2]; } MsxFwPersist;
uint32_t msx_fw_persist(MSX *m, MsxFwPersist *p);
#endif
