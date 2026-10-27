/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * cputest.c - Z80 코어 단위 시험: T-state, M1 대기, R 레지스터, 인터럽트
 * (ZEXDOC/ZEXALL은 결과만 검사하므로 타이밍은 여기서 검사한다)
 */
#include "../src/z80.h"
#include <stdio.h>
#include <string.h>

static uint8_t mem[65536];
static uint8_t mread(void *c, uint16_t a) { (void)c; return mem[a]; }
static void mwrite(void *c, uint16_t a, uint8_t v) { (void)c; mem[a] = v; }
static uint8_t ioread(void *c, uint16_t p) { (void)c; (void)p; return 0xFF; }
static void iowrite(void *c, uint16_t p, uint8_t v) { (void)c; (void)p; (void)v; }

static int fails, total;

static void setup(Z80 *z, int m1)
{
    memset(mem, 0, sizeof mem);
    z80_init(z);
    z->mem_read = mread; z->mem_write = mwrite;
    z->io_read = ioread; z->io_write = iowrite;
    z->m1_wait = (uint8_t)m1;
    z->sp.w = 0xF000;
}

/* bytes를 0x1000에 놓고 1개 실행. 기대 T-state(표준)와 M1 수를 검사 */
static void tcase(const char *name, const uint8_t *bytes, int len,
                  int expect, int m1count, void (*prep)(Z80 *))
{
    for (int m1 = 0; m1 <= 1; m1++) {
        Z80 z;
        setup(&z, m1);
        memcpy(mem + 0x1000, bytes, (size_t)len);
        z.pc = 0x1000;
        if (prep) prep(&z);
        uint8_t r0 = z.r;
        int got = z80_step(&z);
        int want = expect + m1 * m1count;
        int rinc = (z.r - r0) & 0x7F;
        total++;
        if (got != want || rinc != m1count) {
            fails++;
            printf("FAIL %-22s m1wait=%d: T=%d (want %d), R+%d (want %d)\n",
                   name, m1, got, want, rinc, m1count);
        }
    }
}

static void p_b1(Z80 *z) { z->bc.b.h = 1; }      /* DJNZ 불성립 */
static void p_b2(Z80 *z) { z->bc.b.h = 2; }      /* DJNZ 성립 */
static void p_zf(Z80 *z) { z->af.b.l = 0x40; }   /* Z=1 */
static void p_nz(Z80 *z) { z->af.b.l = 0x00; }   /* Z=0 */
static void p_bc1(Z80 *z) { z->bc.w = 1; z->hl.w = 0x2000; z->de.w = 0x3000; }
static void p_bc2(Z80 *z) { z->bc.w = 2; z->hl.w = 0x2000; z->de.w = 0x3000; }

#define T(name, exp, m1, prep, ...) do { \
    static const uint8_t b_[] = { __VA_ARGS__ }; \
    tcase(name, b_, (int)sizeof b_, exp, m1, prep); } while (0)

int main(void)
{
    /* 기본 */
    T("NOP", 4, 1, 0, 0x00);
    T("LD BC,nn", 10, 1, 0, 0x01, 0x34, 0x12);
    T("LD (HL),n", 10, 1, 0, 0x36, 0x55);
    T("INC (HL)", 11, 1, 0, 0x34);
    T("ADD HL,BC", 11, 1, 0, 0x09);
    T("LD (nn),HL", 16, 1, 0, 0x22, 0x00, 0x20);
    T("LD A,(nn)", 13, 1, 0, 0x3A, 0x00, 0x20);
    T("EX (SP),HL", 19, 1, 0, 0xE3);
    T("LD SP,HL", 6, 1, 0, 0xF9);
    T("PUSH BC", 11, 1, 0, 0xC5);
    T("POP BC", 10, 1, 0, 0xC1);
    T("CALL nn", 17, 1, 0, 0xCD, 0x00, 0x20);
    T("RET", 10, 1, 0, 0xC9);
    T("RST 38", 11, 1, 0, 0xFF);
    T("OUT (n),A", 11, 1, 0, 0xD3, 0x98);
    T("IN A,(n)", 11, 1, 0, 0xDB, 0x99);
    /* 조건 */
    T("DJNZ not taken", 8, 1, p_b1, 0x10, 0xFE);
    T("DJNZ taken", 13, 1, p_b2, 0x10, 0xFE);
    T("JR Z taken", 12, 1, p_zf, 0x28, 0x02);
    T("JR Z not taken", 7, 1, p_nz, 0x28, 0x02);
    T("RET Z taken", 11, 1, p_zf, 0xC8);
    T("RET Z not taken", 5, 1, p_nz, 0xC8);
    T("CALL Z taken", 17, 1, p_zf, 0xCC, 0x00, 0x20);
    T("CALL Z not taken", 10, 1, p_nz, 0xCC, 0x00, 0x20);
    T("JP Z", 10, 1, p_nz, 0xCA, 0x00, 0x20);
    /* CB */
    T("RLC B", 8, 2, 0, 0xCB, 0x00);
    T("RLC (HL)", 15, 2, 0, 0xCB, 0x06);
    T("BIT 0,(HL)", 12, 2, 0, 0xCB, 0x46);
    T("SET 0,(HL)", 15, 2, 0, 0xCB, 0xC6);
    /* ED */
    T("IN B,(C)", 12, 2, 0, 0xED, 0x40);
    T("OUT (C),A", 12, 2, 0, 0xED, 0x79);
    T("SBC HL,BC", 15, 2, 0, 0xED, 0x42);
    T("LD (nn),BC", 20, 2, 0, 0xED, 0x43, 0x00, 0x20);
    T("NEG", 8, 2, 0, 0xED, 0x44);
    T("IM 1", 8, 2, 0, 0xED, 0x56);
    T("LD A,I", 9, 2, 0, 0xED, 0x57);
    T("RLD", 18, 2, 0, 0xED, 0x6F);
    T("RETI", 14, 2, 0, 0xED, 0x4D);
    T("LDI", 16, 2, p_bc1, 0xED, 0xA0);
    T("LDIR last", 16, 2, p_bc1, 0xED, 0xB0);
    T("LDIR repeat", 21, 2, p_bc2, 0xED, 0xB0);
    T("ED NOP", 8, 2, 0, 0xED, 0x00);
    /* DD / FD */
    T("LD IX,nn", 14, 2, 0, 0xDD, 0x21, 0x00, 0x20);
    T("LD A,(IX+d)", 19, 2, 0, 0xDD, 0x7E, 0x05);
    T("LD (IX+d),A", 19, 2, 0, 0xDD, 0x77, 0x05);
    T("LD (IX+d),n", 19, 2, 0, 0xDD, 0x36, 0x05, 0x99);
    T("INC (IX+d)", 23, 2, 0, 0xDD, 0x34, 0x05);
    T("ADD A,(IX+d)", 19, 2, 0, 0xDD, 0x86, 0x05);
    T("ADD IX,BC", 15, 2, 0, 0xDD, 0x09);
    T("INC IX", 10, 2, 0, 0xDD, 0x23);
    T("PUSH IX", 15, 2, 0, 0xDD, 0xE5);
    T("POP IX", 14, 2, 0, 0xDD, 0xE1);
    T("EX (SP),IX", 23, 2, 0, 0xDD, 0xE3);
    T("JP (IX)", 8, 2, 0, 0xDD, 0xE9);
    T("LD SP,IX", 10, 2, 0, 0xDD, 0xF9);
    T("LD (nn),IX", 20, 2, 0, 0xDD, 0x22, 0x00, 0x20);
    T("INC IXH", 8, 2, 0, 0xDD, 0x24);
    T("LD IXH,n", 11, 2, 0, 0xDD, 0x26, 0x10);
    T("LD IY,nn", 14, 2, 0, 0xFD, 0x21, 0x00, 0x20);
    T("BIT 0,(IX+d)", 20, 2, 0, 0xDD, 0xCB, 0x05, 0x46);
    T("RLC (IX+d)", 23, 2, 0, 0xDD, 0xCB, 0x05, 0x06);
    T("SET 0,(IY+d),B", 23, 2, 0, 0xFD, 0xCB, 0x05, 0xC0);
    T("DD NOP", 8, 2, 0, 0xDD, 0x00);
    T("DD DD NOP", 12, 3, 0, 0xDD, 0xDD, 0x00);

    /* 인터럽트: IM 1, EI 지연, HALT 해제 */
    {
        Z80 z;
        setup(&z, 1);
        /* 0x1000: EI ; NOP ; HALT */
        mem[0x1000] = 0xFB; mem[0x1001] = 0x00; mem[0x1002] = 0x76;
        z.pc = 0x1000; z.im = 1;
        z80_set_int(&z, 1);
        z80_step(&z);                       /* EI */
        total++;
        int t = z80_step(&z);               /* EI 직후: 인터럽트 수락하지 않고 NOP 실행 */
        if (z.pc != 0x1002 || t != 5) { fails++; printf("FAIL EI delay: pc=%04X t=%d\n", z.pc, t); }
        total++;
        t = z80_step(&z);                   /* 이제 수락: IM 1 = 13 + M1 대기 1 */
        if (z.pc != 0x0038 || t != 14 || z.iff1) { fails++; printf("FAIL IM1 accept: pc=%04X t=%d\n", z.pc, t); }
        total++;
        uint16_t ret = (uint16_t)(mem[z.sp.w] | (mem[z.sp.w + 1] << 8));
        if (ret != 0x1002) { fails++; printf("FAIL IM1 return address %04X\n", ret); }
    }
    {
        Z80 z;
        setup(&z, 1);
        mem[0x1000] = 0x76;                 /* HALT */
        z.pc = 0x1000; z.im = 1; z.iff1 = z.iff2 = 1;
        z80_step(&z);
        total++;
        int t = z80_step(&z);
        if (!z.halted || t != 5 || z.pc != 0x1001) { fails++; printf("FAIL HALT idle: t=%d pc=%04X\n", t, z.pc); }
        z80_set_int(&z, 1);
        z80_step(&z);
        total++;
        uint16_t ret = (uint16_t)(mem[z.sp.w] | (mem[z.sp.w + 1] << 8));
        if (z.halted || z.pc != 0x0038 || ret != 0x1001) { fails++; printf("FAIL HALT wake: pc=%04X ret=%04X\n", z.pc, ret); }
    }
    {
        Z80 z;
        setup(&z, 0);
        mem[0x1000] = 0x00;
        z.pc = 0x1000; z.im = 2; z.i = 0x40; z.iff1 = 1;
        mem[0x40FF] = 0x34; mem[0x4100] = 0x12;
        z80_set_int(&z, 1);
        total++;
        int t = z80_step(&z);
        if (z.pc != 0x1234 || t != 19) { fails++; printf("FAIL IM2: pc=%04X t=%d\n", z.pc, t); }
    }
    {   /* R 레지스터 비트 7 보존 */
        Z80 z;
        setup(&z, 0);
        z.r = 0xFF; z.pc = 0x1000;
        z80_step(&z);
        total++;
        if (z.r != 0x80) { fails++; printf("FAIL R wrap: %02X\n", z.r); }
    }

    printf("cputest: %d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
