/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* medit.h - 머신 편집기 자료 (2.9): blueMSX config.ini를 읽어 고치고 다시 쓴다.
 * 모르는 구역·키는 그대로 보존한다. 호스트에서 시험 (tests/medittest.c). */
#ifndef X86MSX_MEDIT_H
#define X86MSX_MEDIT_H
#include <stdint.h>

#define ME_SLOTS 64
#define ME_OTHER 2048

typedef struct {
    int ps, ss, start, count, type;     /* 기본 슬롯, 서브슬롯, 시작 블록(8 KiB), 블록 수, blueMSX 형식 번호 */
    char file[200];                     /* ROM 경로 (USB 최상위 기준, 예: Machines/MSX - .../bios.rom) */
    char extra[64];                     /* 둘째 따옴표 값 (보존) */
} MeSlot;

typedef struct {
    char name[96];                      /* [General] Id = 폴더 이름 */
    char manufacturer[32], model[32], system[16], keylayout[16], board[24];
    int subslotted[4];
    int ext_ps[5], ext_ss[5];           /* 외부 슬롯 A·B (3.3.1: C·D, -1 = 없음) */
    int cmos, battery;
    char vdp[16];                       /* TMS99x8A, TMS9929A, V9938, V9958 */
    int vram;                           /* KiB */
    int fdc;                            /* 드라이브 수 (-1 = 키 없음) */
    uint32_t z80hz, r800hz;
    MeSlot slot[ME_SLOTS];
    int nslot;
    char other[ME_OTHER];               /* 편집하지 않는 구역 (원문 그대로) */
    char sec_extra[9][192];             /* 아는 구역 안의 모르는 키 (그 구역 끝에 다시 씀) */
} MeMachine;

int me_parse(const char *text, uint32_t len, MeMachine *m);
/* config.ini 글로. 쓴 길이 (모자라면 -1) */
int me_write(const MeMachine *m, char *out, int max);

/* 저장 전 검사. exists(path)는 파일이 있으면 1 (NULL이면 파일 검사 생략).
 * 문제마다 msgs[i]에 한 줄 (영문 키워드 + 값). 문제 수 */
#define ME_MSG 96
int me_check(const MeMachine *m, int (*exists)(const char *path), char (*msgs)[ME_MSG], int max);

/* 형식 (2.15: blueMSX 머신 편집기 목록과 같은 순서·이름) */
enum { ME_K_RAM, ME_K_RAMFIX, ME_K_MAPRAM, ME_K_ROM, ME_K_WIN, ME_K_FULL, ME_K_NOFILE, ME_K_DEV, ME_K_DEVFILE };
/* RAM: 크기 고름(8~64 KB) · RAMFIX: 크기 고정 · MAPRAM: 0000~FFFF, 크기 64 KB~4 MB · ROM: 파일 크기만큼
 * WIN: 고정 창(win 블록) · FULL: 0000~FFFF · NOFILE: 파일 없는 메모리 장치 · DEV: 메모리 없는 장치 · DEVFILE: 메모리 없고 롬 파일 필요 */
typedef struct { int t; const char *name; uint8_t kind, win; } MeTypeInfo;
int me_type_count(void);
const MeTypeInfo *me_type_at(int i);
int me_type_index(int type);
const MeTypeInfo *me_type_info(int type);
const char *me_type_name(int type);                 /* 모르면 NULL */
int me_type_io(int type);                           /* 블록 매핑이 없는 장치인가 */
int me_type_rom(int type);                          /* ROM 파일이 필요한 형식인가 */
int me_window(int type, int count);                 /* 메모리 창 (8 KiB 블록 수, 0 = 없음) */
int me_size_options(int type, int *pages, int max); /* 고를 수 있는 크기 (RAM 종류만) */
int me_db_type(const char *dbtype);
int me_type_supported(int type);                    /* X86MSX가 켤 수 있는 형식인가 */                 /* 롬 DB <type> → 형식 번호 (모르면 -1) */
#endif
