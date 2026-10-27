/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* nicdb.h - 5.2.101 랜카드 표 (구조개편 표준: 새 랜카드 = 드라이버 파일 하나 + 이 표 한 줄).
 * 표는 데이터만 갖고 하드웨어를 건드리지 않는다. 찾기(PCI·USB 훑기)는 플랫폼 쪽(uefi/main.c, netstack.c)이 하고 여기서 이름·종류·드라이버를 알아낸다.
 * 드라이버가 없는 줄(drv=0)도 넣는다: 메뉴에 "유선/무선" 이 보이고, 상태에 "장치는 있지만 드라이버 없음" 을 정확히 말하기 위해서. */
#ifndef NICDB_H
#define NICDB_H
#include <stdint.h>
#include "nic.h"

enum { NB_PCI = 1, NB_USB = 2 };          /* 붙는 길 */
enum { NK_WIRED = 1, NK_WIFI = 2 };       /* 종류: 유선 / 무선 */
#define NID_ANY 0xFFFFu                   /* ven·dev 아무거나 */

typedef struct NicId {
    uint8_t  bus, kind;
    uint16_t ven, dev;                    /* PCI: 제조사·장치 ID, USB: VID·PID (dev=NID_ANY: 그 제조사 전부) */
    uint32_t cls;                         /* 0 = ID로 찾음. PCI: 클래스 상위 16비트(0200 이더넷, 0280 무선), USB: (클래스<<8)|하위클래스 — 규격 클래스로 찾는 줄 */
    const char *chip;                     /* 칩 이름 (로그·상태 표시) */
    const Nic *drv;                       /* X86MSX 드라이버 (0 = 아직 없음) */
} NicId;

const NicId *nicdb_pci(uint16_t ven, uint16_t dev, uint32_t cls16);           /* 맞는 줄 (ID 먼저, 없으면 클래스) 또는 0 */
const NicId *nicdb_usb(uint16_t vid, uint16_t pid, uint8_t cls, uint8_t sub);
int nicdb_count(void);
const NicId *nicdb_at(int i);
#endif
