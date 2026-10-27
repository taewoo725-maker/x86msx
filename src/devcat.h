/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * devcat.h - 장치 목록(카탈로그) (5.2.8 구조 개편 5단계)
 *
 * config.ini [Slots] 의 형식 번호(blueMSX 번호) 하나 = 줄 하나: 이름, 머신 편집기에서의 모양, 기종을 읽을 때 무엇이 되는지.
 * 기종 불러오기(profile.c)와 머신 편집기(medit.c)가 모두 이 표만 본다 — 새 장치 = 이 표에 줄 하나 (+ 그 장치 부품).
 *   블록 없는 줄(블록 수 0): io (DC_IO_*) — 기종 기능 깃발(DF_*)·한자 롬·사전 롬·인식만
 *   블록 있는 줄: mem (DC_MEM_*) — 메모리 영역(RegionType)·매퍼 RAM·MSX-AUDIO 롬·기종 내장 카트리지(MapperType + 파일 없을 때 버퍼)
 */
#ifndef MSX_DEVCAT_H
#define MSX_DEVCAT_H
#include <stdint.h>

enum { DC_ED_RAM, DC_ED_RAMFIX, DC_ED_MAPRAM, DC_ED_ROM, DC_ED_WIN, DC_ED_FULL, DC_ED_NOFILE, DC_ED_DEV, DC_ED_DEVFILE };   /* 편집기: 크기·창 정하는 법 */
enum { DC_IO_NONE, DC_IO_FEATURE, DC_IO_SILENT, DC_IO_KANJI, DC_IO_JISYO };
enum { DC_MEM_NONE, DC_MEM_REGION, DC_MEM_MAPRAM, DC_MEM_AUDIO, DC_MEM_CART };
enum { DC_BUF_FIXED, DC_BUF_EXTRAM, DC_BUF_MEGARAM };
enum {                                  /* 기종 기능 깃발 (MachineProfile 의 칸) */
    DF_NONE, DF_S1985, DF_S1990, DF_TRTIMER, DF_PCM, DF_TRPAUSE, DF_MIDI, DF_MATSU, DF_MATSU_TURBO,
    DF_Y8950, DF_JOYREX, DF_MOON, DF_F4N, DF_F4I, DF_OPLL
};
#define DC_HIDDEN   1                   /* 머신 편집기 목록에 안 나옴 (기종 파일에서만) */
#define DC_FULLSLOT 2                   /* 슬롯 전체(8블록)를 차지 — 블록 수 = 크기 (22·44·69) */

typedef struct {
    int16_t type;                       /* config.ini 형식 번호 */
    const char *name;                   /* blueMSX 2.8.2 머신 편집기 이름 */
    uint8_t ed_kind, ed_win;            /* 편집기 (DC_ED_*, 고정 창 8 KiB 블록 수) */
    uint8_t io, io_arg;                 /* 블록 없는 줄 */
    uint8_t mem, mem_arg, mem_feat;     /* 블록 있는 줄: 영역 종류 또는 매퍼, 함께 켜는 기능 */
    uint8_t buf_rule; uint16_t buf_kb; uint8_t buf_fill;   /* 파일 없는 내장 카트리지 버퍼 */
    uint8_t flags;
} DevInfo;

const DevInfo *devcat_find(int type);   /* 없으면 0 */
int devcat_count(void);                 /* 전체 줄 수 */
int devcat_editor_count(void);          /* 편집기에 보이는 줄 수 (표 앞쪽부터) */
const DevInfo *devcat_at(int i);
int devcat_supported(int type);         /* X86MSX 가 켜는 형식인가 */

#endif
