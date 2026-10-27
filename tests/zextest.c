/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * zextest.c - Z80 코어 검증: ZEXDOC/ZEXALL을 최소 CP/M 환경에서 실행
 *
 * 0x0000: HALT (프로그램 종료 = JP 0 / RST 0)
 * 0x0005: RET   (BDOS 진입점. PC가 5가 되면 C=2/C=9 출력 처리)
 * 0x0006: 스택 꼭대기 주소 (ZEX가 읽어 SP로 사용)
 * 0x0100: .COM 프로그램
 *
 * 사용법: zextest <file.com> [m1wait]
 */
#include "../src/z80.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static uint8_t mem[65536];

static uint8_t mread(void *c, uint16_t a) { (void)c; return mem[a]; }
static void mwrite(void *c, uint16_t a, uint8_t v) { (void)c; mem[a] = v; }
static uint8_t ioread(void *c, uint16_t p) { (void)c; (void)p; return 0xFF; }
static void iowrite(void *c, uint16_t p, uint8_t v) { (void)c; (void)p; (void)v; }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: zextest file.com [m1wait]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    size_t n = fread(mem + 0x100, 1, 0x10000 - 0x100, f);
    fclose(f);

    mem[0x0000] = 0x76;         /* HALT */
    mem[0x0005] = 0xC9;         /* RET */
    mem[0x0006] = 0x00;         /* 스택 꼭대기 0xF000 */
    mem[0x0007] = 0xF0;

    Z80 z;
    z80_init(&z);
    z.mem_read = mread; z.mem_write = mwrite;
    z.io_read = ioread; z.io_write = iowrite;
    z.m1_wait = (uint8_t)(argc > 2 ? atoi(argv[2]) : 0);
    z.pc = 0x0100;
    z.sp.w = 0xF000;

    printf("loaded %zu bytes, m1_wait=%d\n", n, z.m1_wait);
    clock_t t0 = clock();
    uint64_t instr = 0;
    int fails = 0;
    char line[256]; int lp = 0;

    for (;;) {
        if (z.pc == 0x0005) {
            if (z.bc.b.l == 2) {
                char ch = (char)z.de.b.l;
                putchar(ch);
                if (lp < 255) line[lp++] = ch;
                if (ch == '\n') { line[lp] = 0; if (strstr(line, "ERROR")) fails++; lp = 0; }
            } else if (z.bc.b.l == 9) {
                for (uint16_t a = z.de.w; mem[a] != '$'; a++) {
                    char ch = (char)mem[a];
                    putchar(ch);
                    if (lp < 255) line[lp++] = ch;
                    if (ch == '\n') { line[lp] = 0; if (strstr(line, "ERROR")) fails++; lp = 0; }
                }
            }
            fflush(stdout);
        }
        if (z.halted && z.pc == 0x0001) break;
        z80_step(&z);
        instr++;
    }
    double sec = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("\n--- done: %llu instructions, %llu T-states, %.1f s (%.0f MHz equiv), ERROR lines: %d\n",
           (unsigned long long)instr, (unsigned long long)z.cycles, sec,
           sec > 0 ? z.cycles / sec / 1e6 : 0.0, fails);
    return fails ? 1 : 0;
}
