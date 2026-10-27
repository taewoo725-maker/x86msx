/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* sccmaptest.c - 코나미 SCC 매퍼 통합 시험: 뱅크 전환, SCC 켜기·끄기, 파형 쓰기·읽기, 소리 출력 */
#include "../host/msx_host.h"
#include <stdio.h>
#include <string.h>
static MSX m;
static uint8_t rom[131072];
int main(int argc, char **argv)
{
    const char *profile = argc > 1 ? argv[1] : "machines/DPC-200";
    for (int k = 0; k < 16; k++) memset(rom + k * 8192, k, 8192);        /* 뱅크마다 그 번호로 */
    static const uint8_t prog[] = {
        0xF3,                               /* DI */
        0xDB,0xA8, 0xE6,0xCF, 0xF6,0x10, 0xD3,0xA8,   /* 페이지 2 = 슬롯 1 (게임이 하는 것처럼 ENASLT 대신 포트 A8h) */
        0x3E,0x3F, 0x32,0x00,0x90,          /* LD A,3Fh ; LD (9000h),A   SCC 켜기 */
        0x3E,0x55, 0x32,0x00,0x98,          /* LD A,55h ; LD (9800h),A   채널 1 파형[0] */
        0x3A,0x00,0x98, 0x32,0x00,0xE0,     /* LD A,(9800h) ; LD (E000h),A */
        0x3E,0x05, 0x32,0x00,0xB0,          /* LD A,5 ; LD (B000h),A     뱅크 3 = 5 */
        0x3A,0x00,0xA0, 0x32,0x01,0xE0,     /* LD A,(A000h) ; LD (E001h),A */
        0x3E,0x02, 0x32,0x00,0x90,          /* LD A,2 ; LD (9000h),A     SCC 끄기 (뱅크 2 = 2) */
        0x3A,0x00,0x98, 0x32,0x02,0xE0,     /* LD A,(9800h) ; LD (E002h),A */
        0x3E,0x3F, 0x32,0x00,0x90,          /* SCC 다시 켜고 채널 1을 울림 */
        0x3E,0xFD, 0x32,0x80,0x98, 0x3E,0x00, 0x32,0x81,0x98,   /* 주기 253 → 440 Hz */
        0x3E,0x0F, 0x32,0x8A,0x98, 0x3E,0x01, 0x32,0x8F,0x98,   /* 음량 15, 채널 1 켜기 */
        0x3E,0xAA, 0x32,0x03,0xE0,          /* 끝 표시 */
        0x18,0xFE,                          /* JR $ */
    };
    rom[0] = 'A'; rom[1] = 'B'; rom[2] = 0x10; rom[3] = 0x40;
    memcpy(rom + 0x10, prog, sizeof prog);
    for (int i = 0; i < 32; i++) rom[0x100 + i] = 0;                       /* 쓰지 않음 */
    char err[256];
    if (msx_host_init(&m, profile, err, sizeof err)) { printf("init: %s\n", err); return 1; }
    msx_insert_cart(&m, 0, rom, sizeof rom, MAPPER_KONAMI_SCC);
    msx_reset(&m);
    /* 파형 나머지를 사각파로 (CPU 대신 직접: 위 프로그램은 [0]만 씀) */
    int fails = 0, total = 0;
    for (int f = 0; f < 600; f++) msx_run_frame(&m);
    for (int i = 0; i < 32; i++) scc_write(&m.cart[0].scc, (uint8_t)i, i < 16 ? 100 : (uint8_t)-100);
    long energy = 0;
    for (int f = 0; f < 30; f++) { msx_run_frame(&m); for (int i = 0; i < m.psg.buf_n; i++) energy += m.psg.buf[i] < 0 ? -m.psg.buf[i] : m.psg.buf[i]; }
    uint8_t r[4];
    for (int i = 0; i < 4; i++) r[i] = m.cpu.mem_read(m.cpu.ctx, (uint16_t)(0xE000 + i));   /* 페이지 3은 RAM 그대로 */
#define CHECK(c, ...) do { total++; int ok_ = (c); fails += !ok_; printf("%s ", ok_ ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); } while (0)
    CHECK(r[3] == 0xAA, "program ran to the end (marker %02X)", r[3]);
    CHECK(r[0] == 0x55, "SCC waveform write/read at 9800h (got %02X)", r[0]);
    CHECK(r[1] == 5, "bank switch B000h=5 -> A000h shows bank 5 (got %02X)", r[1]);
    CHECK(r[2] == 2, "SCC off (9000h=2) -> 9800h shows ROM bank 2 (got %02X)", r[2]);
    CHECK(m.cart[0].scc.enable == 1 && m.cart[0].scc.period[0] == 253 && m.cart[0].scc.vol[0] == 15, "SCC registers set by program (enable %d period %d vol %d)",
          m.cart[0].scc.enable, m.cart[0].scc.period[0], m.cart[0].scc.vol[0]);
    CHECK(energy > 100000, "SCC tone reaches the audio output (energy %ld)", energy);
    /* SCC-I (SCC+): 모드 레지스터, SCC-I 레지스터 창(채널 5 자기 파형), RAM 쓰기 */
    static uint8_t ram[131072];
    memset(ram, 0, sizeof ram);
    static const uint8_t prog2[] = {
        0xF3, 0xDB,0xA8, 0xE6,0xCF, 0xF6,0x10, 0xD3,0xA8,                    /* DI, 페이지 2 = 슬롯 1 */
        0x3E,0x20, 0x32,0xFE,0xBF,          /* 모드 = SCC-I */
        0x3E,0x80, 0x32,0x00,0xB0,          /* 뱅크 3 비트 7: B800h 레지스터 창 */
        0x3E,0x33, 0x32,0x80,0xB8,          /* 채널 5 파형[0] = 33h */
        0x3E,0x44, 0x32,0x60,0xB8,          /* 채널 4 파형[0] = 44h */
        0x3A,0x80,0xB8, 0x32,0x00,0xE0,     /* 읽기 → E000h */
        0x3A,0x60,0xB8, 0x32,0x01,0xE0,     /* 읽기 → E001h */
        0x3E,0x10, 0x32,0xFE,0xBF,          /* 모드 = 모든 뱅크 RAM */
        0x3E,0x77, 0x32,0x00,0x80,          /* RAM 쓰기 8000h */
        0x3A,0x00,0x80, 0x32,0x02,0xE0,     /* 읽기 → E002h */
        0x3E,0xAA, 0x32,0x03,0xE0, 0x18,0xFE,
    };
    memcpy(ram + 0x10, prog2, sizeof prog2);
    ram[0] = 'A'; ram[1] = 'B'; ram[2] = 0x10; ram[3] = 0x40;
    if (msx_host_init(&m, profile, err, sizeof err)) { printf("init: %s\n", err); return 1; }
    msx_insert_cart(&m, 0, ram, sizeof ram, MAPPER_SCCPLUS);
    msx_reset(&m);
    for (int f = 0; f < 600; f++) msx_run_frame(&m);           /* DPC-200은 로고 뒤 약 250프레임에 카트리지 실행 */
    if (m.cpu.mem_read(m.cpu.ctx, 0xE003) != 0xAA)
        printf("  (pc %04X mode %02X banks %d %d %d %d)\n", m.cpu.pc, m.cart[0].scc_mode, m.cart[0].bank[0], m.cart[0].bank[1], m.cart[0].bank[2], m.cart[0].bank[3]);
    for (int i = 0; i < 4; i++) r[i] = m.cpu.mem_read(m.cpu.ctx, (uint16_t)(0xE000 + i));
    CHECK(r[3] == 0xAA, "SCC-I program ran to the end");
    CHECK(r[0] == 0x33 && r[1] == 0x44, "SCC-I: channel 5 has its own waveform (ch5 %02X, ch4 %02X)", r[0], r[1]);
    CHECK(r[2] == 0x77 && ram[2 * 8192] == 0x77, "SCC-I RAM write at 8000h (read %02X)", r[2]);
    /* 3.4.1 Ese-SCC: 0x7FFF 비트 4 = SRAM 쓰기, 코나미 SCC 뱅크, SCC 켜기 */
    for (int k = 0; k < 16; k++) memset(ram + k * 8192, k, 8192);
    static const uint8_t prog3[] = {
        0xF3, 0xDB,0xA8, 0xE6,0xCF, 0xF6,0x10, 0xD3,0xA8,
        0x3E,0x10, 0x32,0xFF,0x7F,          /* 쓰기 허용 */
        0x3E,0x66, 0x32,0x00,0x60,          /* SRAM 뱅크 1 [0] = 66h */
        0x3A,0x00,0x60, 0x32,0x00,0xE0,
        0xAF, 0x32,0xFF,0x7F,               /* 쓰기 금지 */
        0x3E,0x03, 0x32,0x00,0x70,          /* 6000h 창 = 뱅크 3 */
        0x3A,0x00,0x60, 0x32,0x01,0xE0,
        0x3E,0x3F, 0x32,0x00,0x90, 0x3E,0x55, 0x32,0x00,0x98, 0x3A,0x00,0x98, 0x32,0x02,0xE0,   /* SCC */
        0x3E,0xAA, 0x32,0x03,0xE0, 0x18,0xFE,
    };
    memcpy(ram + 0x10, prog3, sizeof prog3);
    ram[0] = 'A'; ram[1] = 'B'; ram[2] = 0x10; ram[3] = 0x40;
    if (msx_host_init(&m, profile, err, sizeof err)) { printf("init: %s\n", err); return 1; }
    msx_insert_cart(&m, 0, ram, sizeof ram, MAPPER_ESESCC);
    msx_reset(&m);
    for (int f = 0; f < 600; f++) msx_run_frame(&m);
    for (int i = 0; i < 4; i++) r[i] = m.cpu.mem_read(m.cpu.ctx, (uint16_t)(0xE000 + i));
    CHECK(r[3] == 0xAA, "Ese-SCC program ran to the end");
    CHECK(r[0] == 0x66 && ram[8192] == 0x66, "Ese-SCC SRAM write with 7FFFh bit 4 (read %02X)", r[0]);
    CHECK(r[1] == 3, "Ese-SCC bank switch 7000h=3 (got %02X)", r[1]);
    CHECK(r[2] == 0x55, "Ese-SCC SCC at 9800h (got %02X)", r[2]);
    /* 3.4.1 SFG: 롬 0x0000부터, 0x3FF0/1 = YM2151. 페이지 0을 카트리지 슬롯으로 바꿔 A4를 울림 */
    {
        static uint8_t sfg[32768];
        memset(sfg, 0xFF, sizeof sfg);
        static const uint8_t regs[][2] = { {0x20,0xC7}, {0x28,0x4A}, {0x30,0}, {0x40,0x01}, {0x60,0x00}, {0x68,0x7F}, {0x70,0x7F}, {0x78,0x7F},
                                           {0x80,0x1F}, {0xA0,0}, {0xC0,0}, {0xE0,0x0F}, {0x08,0x08} };
        uint8_t *q = sfg + 0x4010;
        *q++ = 0xF3; *q++ = 0xDB; *q++ = 0xA8; *q++ = 0xE6; *q++ = 0xFC; *q++ = 0xF6; *q++ = 0x01; *q++ = 0xD3; *q++ = 0xA8;
        for (unsigned i = 0; i < sizeof regs / sizeof regs[0]; i++) {
            *q++ = 0x3E; *q++ = regs[i][0]; *q++ = 0x32; *q++ = 0xF0; *q++ = 0x3F;
            *q++ = 0x3E; *q++ = regs[i][1]; *q++ = 0x32; *q++ = 0xF1; *q++ = 0x3F;
        }
        *q++ = 0x3A; *q++ = 0xF1; *q++ = 0x3F; *q++ = 0x32; *q++ = 0x01; *q++ = 0xE0;   /* 상태 → E001h */
        *q++ = 0xDB; *q++ = 0xA8; *q++ = 0xE6; *q++ = 0xFC; *q++ = 0xD3; *q++ = 0xA8;
        *q++ = 0x3E; *q++ = 0xAA; *q++ = 0x32; *q++ = 0x03; *q++ = 0xE0; *q++ = 0x18; *q++ = 0xFE;
        sfg[0x4000] = 'A'; sfg[0x4001] = 'B'; sfg[0x4002] = 0x10; sfg[0x4003] = 0x40;
        if (msx_host_init(&m, profile, err, sizeof err)) { printf("init: %s\n", err); return 1; }
        msx_insert_cart(&m, 0, sfg, sizeof sfg, MAPPER_SFG);
        msx_reset(&m);
        for (int f = 0; f < 600; f++) msx_run_frame(&m);
        long e2 = 0;
        for (int f = 0; f < 30; f++) { msx_run_frame(&m); for (int i = 0; i < m.psg.buf_n; i++) e2 += m.psg.buf[i] < 0 ? -m.psg.buf[i] : m.psg.buf[i]; }
        CHECK(m.cpu.mem_read(m.cpu.ctx, 0xE003) == 0xAA, "SFG program ran to the end");
        CHECK(m.opm.reg[0x28] == 0x4A && m.opm.op[0].key, "SFG: YM2151 registers written through 3FF0h/3FF1h");
        CHECK(e2 > 100000, "SFG tone reaches the audio output (energy %ld)", e2);
    }
    printf("sccmaptest: %d/%d passed\n", total - fails, total);
    return fails;
}
