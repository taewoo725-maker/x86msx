/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* tapetest.c - 카세트 테이프: CSAVE → NEW → 되감기 → CLOAD, BLOAD"CAS:",R (DPC-200 BIOS) */
#include "../host/msx_host.h"
#include <stdio.h>
#include <string.h>
static MSX m;
static uint8_t tape[65536];
static const uint8_t hdr[8] = { 0x1F, 0xA6, 0xDE, 0xBA, 0xCC, 0x13, 0x7D, 0x74 };

static void type_text(const char *t)                   /* BIOS 키 버퍼에 글자 넣기 ('|' = Enter), 버퍼가 비면 다음 */
{
    while (*t) {
        for (int f = 0; f < 600; f++) {
            uint16_t get = (uint16_t)(m.cpu.mem_read(m.cpu.ctx, 0xF3FA) | (m.cpu.mem_read(m.cpu.ctx, 0xF3FB) << 8));
            uint16_t put = (uint16_t)(m.cpu.mem_read(m.cpu.ctx, 0xF3F8) | (m.cpu.mem_read(m.cpu.ctx, 0xF3F9) << 8));
            if (get == put) break;
            msx_run_frame(&m);
        }
        uint16_t n = 0;
        for (; *t && n < 30; t++, n++) m.cpu.mem_write(m.cpu.ctx, (uint16_t)(0xFBF0 + n), (uint8_t)(*t == '|' ? 13 : *t));
        m.cpu.mem_write(m.cpu.ctx, 0xF3F8, (uint8_t)((0xFBF0 + n) & 0xFF)); m.cpu.mem_write(m.cpu.ctx, 0xF3F9, (uint8_t)((0xFBF0 + n) >> 8));
        m.cpu.mem_write(m.cpu.ctx, 0xF3FA, 0xF0); m.cpu.mem_write(m.cpu.ctx, 0xF3FB, 0xFB);
        for (int f = 0; f < 60; f++) msx_run_frame(&m);
    }
    for (int f = 0; f < 120; f++) msx_run_frame(&m);
}

static int screen_has(const char *s)
{
    char t[24 * 41 + 1]; int k = 0;
    uint16_t nt = (uint16_t)((m.vdp.reg[2] & 0x0F) << 10);
    int cols = (m.vdp.reg[1] & 0x10) ? 40 : 32;
    for (int i = 0; i < 24 * cols; i++) { uint8_t c = m.vdp.vram[nt + i]; t[k++] = (c >= 32 && c < 127) ? (char)c : ' '; }
    t[k] = 0;
    return strstr(t, s) != 0;
}

int main(int argc, char **argv)
{
    const char *profile = argc > 1 ? argv[1] : "machines/DPC-200";
    char err[256]; int fails = 0, total = 0;
#define CHECK(c, ...) do { total++; int ok_ = (c); fails += !ok_; printf("%s ", ok_ ? "PASS" : "FAIL"); printf(__VA_ARGS__); printf("\n"); } while (0)
    if (msx_host_init(&m, profile, err, sizeof err)) { printf("init: %s\n", err); return 1; }
    msx_tape_insert(&m, tape, 0, sizeof tape);
    for (int f = 0; f < 400; f++) msx_run_frame(&m);
    type_text("10 PRINT 123|csave\"TEST\"|");
    CHECK(m.tape_size > 16 && !memcmp(m.tape, hdr, 8), "CSAVE wrote a block with CAS header (%u bytes)", m.tape_size);
    int names = 0; for (uint32_t i = 0; i + 4 < m.tape_size; i++) if (!memcmp(m.tape + i, "TEST", 4)) names++;
    CHECK(names == 1, "CSAVE file name in the header block");
    uint32_t saved = m.tape_size;
    type_text("new|");
    m.tape_pos = 0;                                     /* 되감기 */
    type_text("cload|list|");
    CHECK(screen_has("10 PRINT 123"), "CLOAD then LIST shows the program");
    CHECK(m.tape_size == saved, "reading does not change the tape");

    /* BLOAD"CAS:",R: 기계어 블록 (머리표 + D0 x10 + 이름, 머리표 + 시작·끝·실행 + 코드) */
    static const uint8_t code[] = { 0x3E, 0x5A, 0x32, 0x00, 0xE1, 0xC9 };          /* LD A,5Ah ; LD (E100h),A ; RET */
    uint32_t p = 0;
    memcpy(tape + p, hdr, 8); p += 8;
    memset(tape + p, 0xD0, 10); p += 10;
    memcpy(tape + p, "BINTST", 6); p += 6;
    while (p & 7) tape[p++] = 0;
    memcpy(tape + p, hdr, 8); p += 8;
    uint16_t start = 0xC000, end = (uint16_t)(0xC000 + sizeof code - 1), exec = 0xC000;
    tape[p++] = (uint8_t)start; tape[p++] = (uint8_t)(start >> 8);
    tape[p++] = (uint8_t)end; tape[p++] = (uint8_t)(end >> 8);
    tape[p++] = (uint8_t)exec; tape[p++] = (uint8_t)(exec >> 8);
    memcpy(tape + p, code, sizeof code); p += sizeof code;
    msx_tape_insert(&m, tape, p, sizeof tape);
    m.cpu.mem_write(m.cpu.ctx, 0xE100, 0);
    type_text("bload\"cas:\",r|");
    CHECK(m.cpu.mem_read(m.cpu.ctx, 0xE100) == 0x5A, "BLOAD\"CAS:\",R loaded and ran machine code (E100h = %02X)", m.cpu.mem_read(m.cpu.ctx, 0xE100));
    CHECK(screen_has("Ok"), "BASIC back at Ok after BLOAD");
    printf("tapetest: %d/%d passed\n", total - fails, total);
    return fails;
}
