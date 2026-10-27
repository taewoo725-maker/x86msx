/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * msxstate.h - 장치별 상태 저장.
 *
 * 상태 = 덩이(chunk)들의 줄: [이름 4글자][판 u32][길이 u32][내용, 4바이트 맞춤]
 *  - 장치마다 덩이 하나 (CPU, VDP, PSG, FDC, …) + 자기 판 번호. 불러올 때 판·길이가 지금 빌드와 같은 덩이만 덮고,
 *    다른 덩이는 건너뜀 (그 장치는 켠 상태 그대로) → 한 장치 구조를 바꿔도 다른 장치 저장은 그대로 읽힘
 *  - CORE = MSX 구조체에서 장치 칸·매퍼 RAM 을 뺀 나머지 (슬롯·매퍼 레지스터·테이프 위치 등).
 *    5.2.87 부터 덩이 "COR2" = 필드 표(msx_st_core): 필드마다 이름·크기·내용. 이름으로 맞춰 덮고, 크기가 바뀐 필드만 건너뜀(켠 상태),
 *    새 필드는 켠 상태 그대로 → MSX 구조체에 필드를 더해도 옛 상태가 읽힘. COR2 판이 다르거나 깨졌으면 불러오기 실패.
 *    5.2.86 까지의 "CORE"(구조체 통째)는 크기가 지금과 같을 때만 읽음
 *  - 모르는 덩이는 건너뜀 (프런트엔드 덩이 FEHD 등)
 */
#ifndef MSXSTATE_H
#define MSXSTATE_H
#include <stdint.h>
#include "msx.h"

#define ST_CHUNK_HDR 12u

/* 덩이 하나 쓰기 (out 이 모자라면 0) — 프런트엔드도 자기 덩이를 같은 꼴로 씀 */
uint32_t st_put(uint8_t *out, uint32_t max, const char tag[4], uint32_t ver, const void *data, uint32_t len);
/* 덩이 찾기: 없으면 NULL */
const uint8_t *st_find(const uint8_t *buf, uint32_t len, const char tag[4], uint32_t *ver, uint32_t *size);

/* 기계 상태를 덩이들로 (돌려줌: 쓴 길이, 0 = 자리 모자람) */
uint32_t msx_st_save(MSX *m, uint8_t *out, uint32_t max);
/* 저장된 n 번 카트리지가 꽂혀 있었는지·매퍼 (불러오기 전에 같은 롬을 다시 꽂을 때) — CART 덩이가 맞지 않으면 -1 */
int msx_st_cart(const uint8_t *buf, uint32_t len, int n, int *present, int *mapper);
/* CORE·MRAM 이 이 빌드와 맞는지 (0 = 맞음) — 기종·매체를 바꾸기 전에 확인 */
int msx_st_check(const uint8_t *buf, uint32_t len);
/* 기계 상태 덮기: 포인터·설정(채널 음량·R800 속도·주사 주파수)은 지금 것.
 * 돌려줌: 0 = 됨, -1 = CORE 가 맞지 않음(COR2 판이 다르거나 깨짐, 옛 CORE 는 크기가 다름). skipped 에 건너뛴 장치 이름 (쉼표, 크기가 바뀐 CORE 필드는 "CORE.이름") */
int msx_st_load(MSX *m, const uint8_t *buf, uint32_t len, char *skipped, int skiplen);

/* CORE 필드 표 (시험용으로도 씀) */
typedef struct { const char *name; uint32_t off, size; } MsxStField;
extern const MsxStField msx_st_core[];
extern const int msx_st_ncore;
/* 시험용: 5.2.86 까지 꼴의 CORE 덩이 (구조체 통째) */
uint32_t msx_st_core_v1(MSX *m, uint8_t *out, uint32_t max);

/* 시험용: 장치 덩이 표 */
typedef struct { char tag[4]; uint32_t off, size, ver; } MsxStDev;
extern const MsxStDev msx_st_devs[];
extern const int msx_st_ndevs;
#endif
