/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* bastest.c - 3.2.7 BASIC 토큰 → 글 시험 */
#include "../kernel/basictxt.h"
#include <stdio.h>
#include <string.h>
static uint8_t mem[65536];
static uint8_t pk(void *c, uint16_t a) { (void)c; return mem[a]; }
int main(void)
{
    uint16_t p = 0x8001;
    mem[0xF676] = 0x01; mem[0xF677] = 0x80;
    /* 10 A=1.5:PRINT LEFT$("A:B",1);&HFF */
    static const uint8_t l1[] = { 'A', 0xEF, 0x1D, 0x41, 0x15, 0x00, 0x00, ':', 0x91, ' ', 0xFF, 0x81, '(', '"', 'A', ':', 'B', '"', ',', 0x12, ')', ';', 0x0C, 0xFF, 0x00, 0 };
    /* 20 GOTO <포인터 → 10> :REM' X */
    static const uint8_t l2[] = { 0x89, ' ', 0x0D, 0x00, 0x80, ':', 0x8F, 0xE6, ' ', 'X', 0 };
    uint16_t a1 = p, a2 = (uint16_t)(a1 + 4 + sizeof l1);
    mem[a1] = (uint8_t)a2; mem[a1 + 1] = (uint8_t)(a2 >> 8); mem[a1 + 2] = 10; memcpy(mem + a1 + 4, l1, sizeof l1);
    uint16_t a3 = (uint16_t)(a2 + 4 + sizeof l2);
    mem[a2] = (uint8_t)a3; mem[a2 + 1] = (uint8_t)(a3 >> 8); mem[a2 + 2] = 20; memcpy(mem + a2 + 4, l2, sizeof l2);
    char out[512]; int lines;
    int n = basic_detok(pk, 0, out, sizeof out, &lines);
    const char *want = "10 A=1.5:PRINT LEFT$(\"A:B\",1);&HFF\r\n20 GOTO 10' X\r\n";
    int ok = n == (int)strlen(want) && !memcmp(out, want, (size_t)n) && lines == 2;
    if (!ok) { printf("got (%d): %.*s\n", n, n > 0 ? n : 0, out); }
    printf("bastest: %s\n", ok ? "1/1 passed" : "FAIL");
    return !ok;
}
