/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
#include "../src/z80.h"
#include <stdio.h>
#include <string.h>
static uint8_t mem[65536];
static uint8_t rd(void *c, uint16_t a) { (void)c; return mem[a]; }
static void wr(void *c, uint16_t a, uint8_t v) { (void)c; mem[a] = v; }
static uint8_t in_(void *c, uint16_t a) { (void)c; (void)a; return 0xFF; }
static void out_(void *c, uint16_t a, uint8_t v) { (void)c; (void)a; (void)v; }
int main(void)
{
    Z80 z; int fails = 0;
    z80_init(&z); z.mem_read = rd; z.mem_write = wr; z.io_read = in_; z.io_write = out_; z80_reset(&z);
    /* LD A,200 ; LD B,3 ; MULUB A,B ; LD HL,1000 ; LD BC,70 ; MULUW HL,BC ; HALT */
    uint8_t p[] = { 0x3E,200, 0x06,3, 0xED,0xC1, 0x22,0x00,0x90, 0x21,0xE8,0x03, 0x01,70,0, 0xED,0xC3, 0x76 };
    memcpy(mem, p, sizeof p);
    z.r800 = 1;
    for (int i = 0; i < 8 && !z.halted; i++) z80_step(&z);
    uint16_t mulub = (uint16_t)(mem[0x9000] | (mem[0x9001] << 8));
    printf("MULUB 200*3 = %u (C %d)\n", mulub, 0);
    printf("MULUW 1000*70 = %u (DE %04X HL %04X, C %d)\n", ((uint32_t)z.de.w << 16) | z.hl.w, z.de.w, z.hl.w, z.af.b.l & 1);
    fails += mulub != 600 || (((uint32_t)z.de.w << 16) | z.hl.w) != 70000 || !(z.af.b.l & 1);
    printf("r800: %s\n", fails ? "FAILED" : "all passed");
    return fails;
}
