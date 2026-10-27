/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* romdb.h - blueMSX 롬 데이터베이스(msxromdb.xml, msxsysromdb.xml) 조회 */
#ifndef MSX_ROMDB_H
#define MSX_ROMDB_H
#include <stdint.h>

typedef struct {
    char type[32];              /* <type> (없으면 "") */
    int start;                  /* <start> 주소 (없으면 -1) */
} RomDbInfo;

/* SHA-1(소문자 16진 40자)을 담은 <dump> 항목을 찾아 그 안의 <type>·<start>를 읽는다. 찾으면 1 */
int romdb_find(const uint8_t *db, uint32_t len, const char *sha1hex, RomDbInfo *out);
/* 3.2.15: X86MSX 이진 롬 DB (tools/mkromdb.py가 openMSX softwaredb에서 만든 Databases\x86msx_romdb.bin).
 * 찾으면 1: *type 1 일반 2 Konami 3 Konami SCC 4 ASCII8 5 ASCII16 6 ASCII8+SRAM 7 ASCII16+SRAM 8 R-Type 9 Harry Fox, *page 시작 페이지(0xFF 자동) */
int romdb_bin_find(const uint8_t *db, uint32_t len, const char *sha1hex, int *type, int *page);
#endif
