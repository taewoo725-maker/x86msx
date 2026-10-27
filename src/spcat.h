/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * spcat.h - 특수 카트리지(외부장치) 목록 (5.2.44, 특수 카트리지 보강 0단계)
 *
 * "파일 ▶ 카트리지 슬롯 n ▶ 특수 카트리지 삽입" 메뉴에 나오는 외부장치 하나 = 이 표의 줄 하나.
 * 메뉴(묶음·차례·구분선)·ini 저장 번호·필요한 롬·만드는 법(how)을 여기서 선언하고, fe.c 는 표를 읽어 메뉴를 만들고 꽂는다.
 * 새 외부장치 = 여기 한 줄 (+ 부품 파일, + 메뉴 글 3개 언어는 kernel/lang.h 의 StrId 와 fe.c 의 kind→StrId 표).
 *
 * kind 번호는 x86msx.ini 의 Special 값(0 → 64, 1 → 65, 나머지 100 + kind)으로 저장되므로 바꾸지 않고 뒤에만 붙인다.
 * 메뉴 차례는 blueMSX+ "Insert Special" 과 같다 (claude 문서: 특수카트리지_보강계획).
 */
#ifndef MSX_SPCAT_H
#define MSX_SPCAT_H
#include <stdint.h>

/* 만드는 법 (fe.c special_apply 의 switch) */
enum { SPK_DISK, SPK_SCC, SPK_FMPAC, SPK_DOS2, SPK_MAPRAM, SPK_SCCI, SPK_IDE, SPK_AUDIO, SPK_AUDIOROM, SPK_PAC, SPK_MEGARAM, SPK_RAM, SPK_JOYREX, SPK_MOON,
       SPK_ESERAM, SPK_MFR, SPK_BEER, SPK_GIDE, SPK_ESESCC, SPK_GOUDA, SPK_CA1, SPK_SFG, SPK_VHD, SPK_MEGASCSI, SPK_WAVESCSI, SPK_V9990, SPK_NET, SPK_FLASH, SPK_HBI55, SPK_MFRP, SPK_RS232, SPK_MSXMIDI,
       SPK_NOT_YET };   /* SPK_NOT_YET: 메뉴에 자리만 (흐림) — blueMSX 에 있지만 아직 안 만든 것 */

/* 메뉴 묶음 (▶ 하위 메뉴). SPG_ROOT = 맨 위 */
enum { SPG_ROOT, SPG_ESESCC, SPG_IDE, SPG_SCSI, SPG_MEGASCSI, SPG_WAVESCSI, SPG_NOWIND, SPG_EXTRAM, SPG_MEGARAM, SPG_ESERAM, SPG_FLASHROM, SPG_FLASHCART,
       SPG_DISK, SPG_AUDIO, SPG_NET, SPG_N };

typedef struct {
    uint8_t id, parent;         /* 묶음 번호, 부모 묶음 */
    const char *name;           /* 영어 이름 (번역이 없을 때) */
    uint8_t pos;                /* 부모 안 차례 (줄·묶음이 같은 번호 공간) */
    uint8_t sep_after;          /* 1 = 뒤에 구분선 */
} SpGroup;

typedef struct {
    int16_t kind;               /* ini 번호 바탕 (sp_code), -1 = 메뉴 자리만(SPK_NOT_YET) */
    uint8_t group;              /* 어느 묶음 */
    uint8_t pos;                /* 묶음 안 차례 */
    uint8_t sep_after;          /* 1 = 뒤에 구분선 */
    const char *name;           /* 영어 이름 (진단·번역 없을 때) */
    uint8_t how;                /* SPK_* */
    int arg;                    /* 크기 KB, 디스크 롬 형식, 매퍼 … */
    const char *files;          /* USB\Machines 아래 롬 후보 (| 로 나눔), 0 = 롬 없음 */
    const char *romtypes;       /* 롬 DB 종류 (SHA-1 로 찾을 때, | 로 나눔), 0 = 이름으로만 */
} SpCart;

extern const SpGroup sp_groups[SPG_N];
const SpCart *spcat_at(int i);          /* i 번째 줄 (0 ~ spcat_count()-1) */
int spcat_count(void);
const SpCart *spcat_kind(int kind);     /* kind 번호로 (없으면 0) */
int spcat_max_kind(void);               /* 가장 큰 kind + 1 */
#endif
