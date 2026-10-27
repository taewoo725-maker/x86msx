/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * carttest.c - 카트리지/매퍼 시험: tests/carts/expected.txt의 각 ROM에 대해
 *   1) 매퍼 자동 추정이 기대값과 같은지
 *   2) 기대 매퍼로 꽂고 부팅했을 때 화면에 기대 문자열이 찍히는지
 */
#include "../host/msx_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static MSX m;

static int screen_has(const char *needle)
{
    int text = (m.vdp.reg[1] & 0x10) != 0, cols = text ? 40 : 32;
    uint16_t nt = (uint16_t)((m.vdp.reg[2] & 0x0F) << 10);
    char buf[24 * 41 + 1];
    int k = 0;
    for (int i = 0; i < 24 * cols; i++) {
        uint8_t ch = m.vdp.vram[nt + i];
        buf[k++] = (ch >= 32 && ch < 127) ? (char)ch : ' ';
    }
    buf[k] = 0;
    /* 줄바꿈으로 끊긴 출력도 찾을 수 있게 줄 단위가 아닌 연속 문자열로 비교 */
    return strstr(buf, needle) != NULL;
}

/* 3.0.9: 일반 롬 자리(머리 고르기)와 R-Type·HarryFox 뱅크 전환을 직접 확인 */
static uint8_t big[384 * 1024];
static uint8_t *scsi_img;                                   /* 3.9.14: SCSI 시험용 메모리 디스크 */
static int scsi_rd(void *c, uint32_t lba, uint8_t *b) { (void)c; memcpy(b, scsi_img + lba * 512, 512); return 0; }
static int scsi_wr(void *c, uint32_t lba, uint8_t *b) { (void)c; memcpy(scsi_img + lba * 512, b, 512); return 0; }
static int expect(const char *what, int got, int want)
{
    printf("%s %-40s %d (want %d)\n", got == want ? "PASS" : "FAIL", what, got, want);
    return got != want;
}
static int mapper_tests(const char *profile, int *total)
{
    char err[256];
    int f = 0;
    /* Leonard처럼: 0쪽 "AB"는 데이터(예약 칸 ≠ 0), 1쪽이 진짜 머리(INIT 0x8010) → 0x4000에서 시작 */
    memset(big, 0, 0x8000);
    big[0] = 'A'; big[1] = 'B'; big[2] = 0x10; big[3] = 0x80; big[10] = 0x8D; big[11] = 0xB1;
    big[0x4000] = 'A'; big[0x4001] = 'B'; big[0x4002] = 0x10; big[0x4003] = 0x80;
    f += expect("plain base: clean header wins", cart_plain_base(big, 0x8000), 2); (*total)++;
    /* 16 KiB 뱅크 번호를 각 뱅크 첫 바이트에 */
    for (uint32_t i = 0; i < sizeof big / 16384; i++) { memset(big + i * 16384, 0, 16384); big[i * 16384] = (uint8_t)i; big[i * 16384 + 8192] = (uint8_t)(0x80 | i); }
    static const struct { MapperType t; uint32_t size; uint16_t wa; uint8_t v; int p1, p2; } c[] = {
        { MAPPER_RTYPE, 384 * 1024, 0xFFFF, 0, 0x17, 0 },      /* 리셋: 0x4000 = 0x17, 0x8000 = 0 */
        { MAPPER_RTYPE, 384 * 1024, 0x4000, 0x05, 0x17, 5 },
        { MAPPER_RTYPE, 384 * 1024, 0x7FFF, 0x1F, 0x17, 0x17 },  /* 비트4가 서면 & 0x17 */
        { MAPPER_RTYPE, 384 * 1024, 0x6000, 0x0F, 0x17, 0x0F },
        { MAPPER_HARRYFOX, 64 * 1024, 0xFFFF, 0, 0, 1 },
        { MAPPER_HARRYFOX, 64 * 1024, 0x6000, 1, 2, 1 },
        { MAPPER_HARRYFOX, 64 * 1024, 0x7000, 1, 0, 3 },
        { MAPPER_ZEMINA80, 64 * 1024, 0xFFFF, 0, 0x100, 0x100 },       /* 3.8.0: Zemina 80 in 1 - 0x4000~0x4003 = 8 KiB 뱅크 (한 번에 한 창) */
        { MAPPER_ZEMINA80, 64 * 1024, 0x4000, 2, 0x101, 0x100 },
        { MAPPER_ZEMINA80, 64 * 1024, 0x4002, 4, 0x100, 0x102 },
        { MAPPER_ZEMINA126, 64 * 1024, 0xFFFF, 0, 0, 0 },               /* 3.8.0: Zemina 126 in 1 - 0x4000/0x4001 = 16 KiB 뱅크 */
        { MAPPER_ZEMINA126, 64 * 1024, 0x4000, 2, 2, 0 },
        { MAPPER_ZEMINA126, 64 * 1024, 0x4001, 3, 0, 3 },
        { MAPPER_ZEMINA126, 64 * 1024, 0x4002, 3, 0, 0 },               /* 그 밖의 주소는 무시 */
        { MAPPER_CROSSBLAIM, 64 * 1024, 0xFFFF, 0, 0, 0 },              /* 3.8.0: Cross Blaim - 4000 창 고정, 0x4045 = 8000 창 */
        { MAPPER_CROSSBLAIM, 64 * 1024, 0x4045, 3, 0, 3 },
        { MAPPER_CROSSBLAIM, 64 * 1024, 0x6000, 3, 0, 0 },              /* 그 밖의 주소는 무시 */
        { MAPPER_LODERUNNER, 128 * 1024, 0x0000, 5, 0, 5 },             /* 3.8.0: Super Lode Runner - 0000~3FFF 쓰기 = 8000 창 (슬롯 선택 없이) */
        { MAPPER_LODERUNNER, 128 * 1024, 0x3FFF, 7, 0, 7 },

    };
    for (unsigned k = 0; k < sizeof c / sizeof c[0]; k++) {
        if (msx_host_init(&m, profile, err, sizeof err)) { printf("init: %s\n", err); return 1; }
        msx_insert_cart(&m, 0, big, c[k].size, c[k].t);
        msx_reset(&m);
        int s = m.cart_slot[0];
        msx_io_write(&m, 0xA8, (uint8_t)((s << 2) | (s << 4)));        /* 0x4000~0xBFFF = 카트리지 슬롯 */
        if (c[k].wa != 0xFFFF) m.cpu.mem_write(m.cpu.ctx, c[k].wa, c[k].v);
        char w[64];
        snprintf(w, sizeof w, "%s case %u 0x4000/0x6000", cart_mapper_name(c[k].t), k);
        f += expect(w, msx_peek(&m, 0x4000) | (msx_peek(&m, 0x6000) == (0x80 | msx_peek(&m, 0x4000)) ? 0 : 0x100), c[k].p1);
        snprintf(w, sizeof w, "%s case %u 0x8000/0xA000", cart_mapper_name(c[k].t), k);
        f += expect(w, msx_peek(&m, 0x8000) | (msx_peek(&m, 0xA000) == (0x80 | msx_peek(&m, 0x8000)) ? 0 : 0x100), c[k].p2);
        *total += 2;
        msx_host_free(&m);
    }
    /* 3.8.0: Zemina 90 in 1 - 포트 77h (비트 0~5 = 16 KiB 세그먼트, 비트 6 = 8000 창 8 KiB 둘 바꿈, 비트 7 = 8000 창이 다음 세그먼트) */
    {
        static const struct { uint8_t v; int p1, p2; } z[] = {
            { 0xFF, 0, 0 },                                   /* 리셋 (쓰기 없음) */
            { 0x02, 2, 2 },                                   /* 세그먼트 2, 8000 창은 거울 */
            { 0x82, 2, 3 },                                   /* 8000 창 = 다음 세그먼트 */
            { 0x42, 2, 0x182 },                               /* 8000 창의 8 KiB 둘이 바뀜 (0x8000 = 뱅크 2의 뒤쪽 8 KiB) */
        };
        for (unsigned k = 0; k < sizeof z / sizeof z[0]; k++) {
            if (msx_host_init(&m, profile, err, sizeof err)) return 1;
            msx_insert_cart(&m, 0, big, 64 * 1024, MAPPER_ZEMINA90);
            msx_reset(&m);
            int s = m.cart_slot[0];
            msx_io_write(&m, 0xA8, (uint8_t)((s << 2) | (s << 4)));
            if (z[k].v != 0xFF) msx_io_write(&m, 0x77, z[k].v);
            char w[64];
            snprintf(w, sizeof w, "Zemina90in1 case %u 0x4000/0x6000", k);
            f += expect(w, msx_peek(&m, 0x4000) | (msx_peek(&m, 0x6000) == (0x80 | msx_peek(&m, 0x4000)) ? 0 : 0x100), z[k].p1);
            snprintf(w, sizeof w, "Zemina90in1 case %u 0x8000/0xA000", k);
            f += expect(w, msx_peek(&m, 0x8000) | (msx_peek(&m, 0xA000) == (0x80 | msx_peek(&m, 0x8000)) ? 0 : 0x100), z[k].p2);
            *total += 2;
            msx_host_free(&m);
        }
    }
    /* 3.8.0: Holy Quran - 0x5000/5400/5800/5C00 (+ 1 KiB 거울) = 4000/6000/8000/A000 8 KiB 뱅크 */
    {
        if (msx_host_init(&m, profile, err, sizeof err)) return 1;
        msx_insert_cart(&m, 0, big, 384 * 1024, MAPPER_QURAN);
        msx_reset(&m);
        int s = m.cart_slot[0];
        msx_io_write(&m, 0xA8, (uint8_t)((s << 2) | (s << 4)));
        m.cpu.mem_write(m.cpu.ctx, 0x5000, 2); m.cpu.mem_write(m.cpu.ctx, 0x5801, 3); m.cpu.mem_write(m.cpu.ctx, 0x5DFF, 5); m.cpu.mem_write(m.cpu.ctx, 0x5401, 4);
        f += expect("Quran 0x4000 (bank 2)", msx_peek(&m, 0x4000), 1);
        f += expect("Quran 0x6000 (bank 4, mirror)", msx_peek(&m, 0x6000), 2);
        f += expect("Quran 0x8000 (bank 3, mirror)", msx_peek(&m, 0x8000), 0x81);
        f += expect("Quran 0xA000 (bank 5)", msx_peek(&m, 0xA000), 0x82);
        *total += 4;
        msx_host_free(&m);
    }
    /* 3.8.0: ASCII8 SRAM 32 KiB - 뱅크 값 아래 두 비트가 8 KiB 칸을 고름, 2 KiB는 거울 */
    {
        static const int kbs[] = { 32, 8, 2 };
        for (unsigned q = 0; q < 3; q++) {
            if (msx_host_init(&m, profile, err, sizeof err)) return 1;
            msx_insert_cart(&m, 0, big, 64 * 1024, MAPPER_ASCII8_SRAM);
            msx_cart_set_sram(&m, 0, kbs[q]);
            msx_reset(&m);
            int s = m.cart_slot[0];
            msx_io_write(&m, 0xA8, (uint8_t)((s << 2) | (s << 4)));
            for (int k = 0; k < 4; k++) { m.cpu.mem_write(m.cpu.ctx, 0x7000, (uint8_t)(8 | k)); m.cpu.mem_write(m.cpu.ctx, 0x8005, (uint8_t)(0xA0 + k)); }
            int bad = 0;
            for (int k = 0; k < 4; k++) {
                m.cpu.mem_write(m.cpu.ctx, 0x7000, (uint8_t)(8 | k));
                int want = kbs[q] == 32 ? 0xA0 + k : kbs[q] == 8 ? 0xA3 : 0xA3;
                if (msx_peek(&m, 0x8005) != want) bad++;
            }
            /* 2 KiB: 0x800 뒤에도 같은 내용 */
            if (kbs[q] == 2) { m.cpu.mem_write(m.cpu.ctx, 0x7000, 8); if (msx_peek(&m, 0x8005 + 0x800) != 0xA3) bad++; }
            char w[64]; snprintf(w, sizeof w, "ASCII8 SRAM %d KiB", kbs[q]);
            f += expect(w, bad, 0);
            *total += 1;
            msx_host_free(&m);
        }
    }
    /* 3.4.4: 2 MiB 롬 (Game World 126 = ASCII16 128뱅크, ASCII8이면 256뱅크) — 1 MiB 뒤 뱅크 */
    {
        static uint8_t g[2u << 20];
        for (uint32_t i = 0; i < sizeof g / 8192; i++) { memset(g + i * 8192, 0, 8192); g[i * 8192] = (uint8_t)i; }
        if (msx_host_init(&m, profile, err, sizeof err)) return 1;
        f += expect("2 MiB ASCII16 insert", msx_insert_cart(&m, 0, g, sizeof g, MAPPER_ASCII16), 0); (*total)++;
        msx_reset(&m);
        int s = m.cart_slot[0];
        msx_io_write(&m, 0xA8, (uint8_t)((s << 2) | (s << 4)));
        m.cpu.mem_write(m.cpu.ctx, 0x7000, 100);            /* 0x8000 창 = 16 KiB 뱅크 100 (1.56 MiB) */
        f += expect("2 MiB ASCII16 bank 100", msx_peek(&m, 0x8000), 200); (*total)++;
        m.cpu.mem_write(m.cpu.ctx, 0x6000, 127);
        f += expect("2 MiB ASCII16 bank 127", msx_peek(&m, 0x4000), 254); (*total)++;
        msx_host_free(&m);
        if (msx_host_init(&m, profile, err, sizeof err)) return 1;
        msx_insert_cart(&m, 0, g, sizeof g, MAPPER_ASCII8);
        msx_reset(&m);
        msx_io_write(&m, 0xA8, (uint8_t)((s << 2) | (s << 4)));
        m.cpu.mem_write(m.cpu.ctx, 0x7000, 0xF3);           /* 0x8000 창 = 8 KiB 뱅크 243 */
        f += expect("2 MiB ASCII8 bank 243", msx_peek(&m, 0x8000), 243); (*total)++;
        msx_host_free(&m);
    }
    /* 3.1.1: Ese-RAM — 비트 7이 선 창만 쓰기, Mega Flash ROM SCC — 프로그램·ID·섹터 지우기 */
    {
        static uint8_t ram[128 * 1024];
        memset(ram, 0xFF, sizeof ram);
        if (msx_host_init(&m, profile, err, sizeof err)) return 1;
        msx_insert_cart(&m, 0, ram, sizeof ram, MAPPER_ESERAM);
        msx_reset(&m);
        int s = m.cart_slot[0];
        msx_io_write(&m, 0xA8, (uint8_t)((s << 2) | (s << 4)));
        m.cpu.mem_write(m.cpu.ctx, 0x7000, 0x85);           /* 0x8000 창 = 뱅크 5, 쓰기 허용 */
        m.cpu.mem_write(m.cpu.ctx, 0x6000, 0x03);           /* 0x4000 창 = 뱅크 3, 쓰기 막음 */
        m.cpu.mem_write(m.cpu.ctx, 0x8123, 0x5A);
        m.cpu.mem_write(m.cpu.ctx, 0x4123, 0x5A);
        f += expect("Ese-RAM write enabled bank", ram[5 * 8192 + 0x123], 0x5A); (*total)++;
        f += expect("Ese-RAM write protected bank", ram[3 * 8192 + 0x123], 0xFF); (*total)++;
        f += expect("Ese-RAM read back", msx_peek(&m, 0x8123), 0x5A); (*total)++;
        msx_host_free(&m);
        static uint8_t fl[512 * 1024];
        memset(fl, 0xFF, sizeof fl);
        if (msx_host_init(&m, profile, err, sizeof err)) return 1;
        msx_insert_cart(&m, 0, fl, sizeof fl, MAPPER_MFRSCC);
        msx_reset(&m);
        msx_io_write(&m, 0xA8, (uint8_t)((s << 2) | (s << 4)));
        /* 0x4000 창 = 뱅크 0: 5555/2AAA는 창 안의 0x555·0x2AA (A0~A10) */
        #define FW(a, v) m.cpu.mem_write(m.cpu.ctx, (uint16_t)(a), (uint8_t)(v))
        FW(0x4555, 0xAA); FW(0x42AA, 0x55); FW(0x4555, 0x90);
        f += expect("MFR autoselect manufacturer", msx_peek(&m, 0x4000), 0x01); (*total)++;
        f += expect("MFR autoselect device", msx_peek(&m, 0x4001), 0xA4); (*total)++;
        FW(0x4000, 0xF0);
        FW(0x4555, 0xAA); FW(0x42AA, 0x55); FW(0x4555, 0xA0); FW(0x4100, 0x3C);
        f += expect("MFR program byte", msx_peek(&m, 0x4100), 0x3C); (*total)++;
        FW(0x9000, 0x09);                                   /* 0x8000 창 = 뱅크 9 (섹터 1) */
        FW(0x8555, 0xAA); FW(0x82AA, 0x55); FW(0x8555, 0xA0); FW(0x8010, 0x11);
        f += expect("MFR program in bank 9", fl[9 * 8192 + 0x10], 0x11); (*total)++;
        FW(0x8555, 0xAA); FW(0x82AA, 0x55); FW(0x8555, 0x80); FW(0x8555, 0xAA); FW(0x82AA, 0x55); FW(0x8000, 0x30);
        f += expect("MFR sector erase", fl[9 * 8192 + 0x10], 0xFF); (*total)++;
        f += expect("MFR other sector kept", fl[0x100], 0x3C); (*total)++;
        msx_host_free(&m);
    }
    /* 4.0.131: Yamanooto·ASCII16-X 플래시 칩 ID(자동 선택)·CFI·쓰기·지우기·바뀐 조각 표시, SCC 좌우 */
    {
        static uint8_t fl[1024 * 1024];
        for (int mp = 0; mp < 2; mp++) {
            memset(fl, 0xFF, sizeof fl); fl[0] = 0x42; fl[0x1C] = 0x77;
            if (msx_host_init(&m, profile, err, sizeof err)) return 1;
            MapperType mt = mp ? MAPPER_ASCII16X : MAPPER_YAMANOOTO;
            msx_insert_cart(&m, 0, fl, sizeof fl, mt);
            msx_reset(&m);
            int s = m.cart_slot[0];
            msx_io_write(&m, 0xA8, (uint8_t)((s << 2) | (s << 4)));
            if (!mp) { FW(0x7FFF, 0x01); FW(0x7FFF, 0x11); }     /* 레지스터 켜기 + 플래시 쓰기 허용 */
            const char *nm = mp ? "ASCII16X" : "Yamanooto";
            char w[64];
            FW(0x4AAA, 0xAA); FW(0x4555, 0x55); FW(0x4AAA, 0x90);
            snprintf(w, sizeof w, "%s autoselect manufacturer", nm); f += expect(w, msx_peek(&m, 0x4000), 0x01); (*total)++;
            snprintf(w, sizeof w, "%s autoselect device", nm); f += expect(w, msx_peek(&m, 0x4002), 0x7E); (*total)++;
            snprintf(w, sizeof w, "%s autoselect 0x0E", nm); f += expect(w, msx_peek(&m, 0x401C), 0x10); (*total)++;
            snprintf(w, sizeof w, "%s autoselect 0x0F", nm); f += expect(w, msx_peek(&m, 0x401E), 0x00); (*total)++;
            FW(0x4000, 0xF0);
            snprintf(w, sizeof w, "%s reset -> data", nm); f += expect(w, msx_peek(&m, 0x4000), 0x42); (*total)++;
            FW(0x40AA, 0x98);
            snprintf(w, sizeof w, "%s CFI Q", nm); f += expect(w, msx_peek(&m, 0x4020), 'Q'); (*total)++;
            snprintf(w, sizeof w, "%s CFI R", nm); f += expect(w, msx_peek(&m, 0x4022), 'R'); (*total)++;
            snprintf(w, sizeof w, "%s CFI Y", nm); f += expect(w, msx_peek(&m, 0x4024), 'Y'); (*total)++;
            snprintf(w, sizeof w, "%s CFI size 2^23", nm); f += expect(w, msx_peek(&m, 0x404E), 0x17); (*total)++;
            FW(0x4000, 0xF0);
            FW(0x4AAA, 0xAA); FW(0x4555, 0x55); FW(0x4AAA, 0xA0); FW(0x4100, 0x0F);
            snprintf(w, sizeof w, "%s program (AND)", nm); f += expect(w, fl[0x100], 0x0F); (*total)++;
            snprintf(w, sizeof w, "%s dirty chunk 0", nm); f += expect(w, m.cart[0].fl_dirty[0] & 1, 1); (*total)++;
            snprintf(w, sizeof w, "%s sram_dirty", nm); f += expect(w, m.cart[0].sram_dirty, 1); (*total)++;
            msx_host_free(&m);
        }
        SCC sc;
        scc_reset(&sc); scc_set_stereo(&sc);
        for (int i = 0; i < 32; i++) scc_write(&sc, (uint8_t)(0x20 + i), 100);        /* 채널 2 파형 */
        scc_write(&sc, 0x82, 0x40); scc_write(&sc, 0x83, 0x00); scc_write(&sc, 0x8B, 15); scc_write(&sc, 0x8F, 0x02);
        static int16_t mo[512], lo[512], ro[512];
        scc_mix_st(&sc, mo, lo, ro, 512, 100);
        long sl = 0, sr = 0, sm = 0;
        for (int i = 100; i < 512; i++) { sl += lo[i] < 0 ? -lo[i] : lo[i]; sr += ro[i] < 0 ? -ro[i] : ro[i]; sm += mo[i] < 0 ? -mo[i] : mo[i]; }
        f += expect("SCC ch2 pan left: L > 2*R", sl > 2 * sr && sm > 0, 1); (*total)++;
    }
    /* 3.9.14: Matra INK (플래시 512 KiB, 읽기 = 첫 64 KiB가 0x0000~0xFFFF, 쓰기 명령 주소 = 주소 + 0x10000) */
    {
        static uint8_t fl[512 * 1024];
        memset(fl, 0xFF, sizeof fl);
        fl[0] = 0x11; fl[0x4000] = 0x22; fl[0xC001] = 0x33;
        if (msx_host_init(&m, profile, err, sizeof err)) return 1;
        msx_insert_cart(&m, 0, fl, sizeof fl, MAPPER_MATRAINK);
        msx_reset(&m);
        int s = m.cart_slot[0];
        msx_io_write(&m, 0xA8, (uint8_t)(s * 0x55));
        #define FW(a, v) m.cpu.mem_write(m.cpu.ctx, (uint16_t)(a), (uint8_t)(v))
        f += expect("INK read page0", msx_peek(&m, 0x0000), 0x11); (*total)++;
        f += expect("INK read page1", msx_peek(&m, 0x4000), 0x22); (*total)++;
        f += expect("INK read page3", msx_peek(&m, 0xC001), 0x33); (*total)++;
        FW(0x0555, 0xAA); FW(0x02AA, 0x55); FW(0x0555, 0xA0); FW(0x0123, 0x3C);
        f += expect("INK program sector 1", fl[0x10123], 0x3C); (*total)++;
        f += expect("INK read unaffected", msx_peek(&m, 0x0123), 0xFF); (*total)++;
        FW(0x4555, 0xAA); FW(0x42AA, 0x55); FW(0x4555, 0x90);
        f += expect("INK autoselect manufacturer", msx_peek(&m, 0x4000), 0x01); (*total)++;
        f += expect("INK autoselect device", msx_peek(&m, 0x4001), 0xA4); (*total)++;
        FW(0x0000, 0xF0);
        f += expect("INK back to data", msx_peek(&m, 0x4000), 0x22); (*total)++;
        FW(0x0555, 0xAA); FW(0x02AA, 0x55); FW(0x0555, 0x80); FW(0x0555, 0xAA); FW(0x02AA, 0x55); FW(0x0000, 0x30);
        f += expect("INK sector erase", fl[0x10123], 0xFF); (*total)++;
        f += expect("INK data kept", fl[0x4000], 0x22); (*total)++;
        msx_host_free(&m);
    }
    /* 3.9.14: MEGA-SCSI / WAVE-SCSI */
    {
        static uint8_t ram[128 * 1024], img[64 * 512];
        for (int i = 0; i < 16; i++) ram[i * 8192] = (uint8_t)i;
        for (int i = 0; i < 64 * 512; i++) img[i] = (uint8_t)(i * 7 + (i >> 9));
        if (msx_host_init(&m, profile, err, sizeof err)) return 1;
        msx_insert_cart(&m, 0, ram, sizeof ram, MAPPER_MEGASCSI);
        msx_reset(&m);
        int s = m.cart_slot[0];
        msx_io_write(&m, 0xA8, (uint8_t)((s << 2) | (s << 4)));
        FW(0x6000, 3); FW(0x6800, 5);
        f += expect("MEGASCSI bank 0x4000", msx_peek(&m, 0x4000), 3); (*total)++;
        f += expect("MEGASCSI bank 0x6000", msx_peek(&m, 0x6000), 5); (*total)++;
        FW(0x7000, 0x82); FW(0x8001, 0x99); FW(0xA001, 0x77);
        f += expect("MEGASCSI SRAM write", ram[2 * 8192 + 1], 0x99); (*total)++;
        f += expect("MEGASCSI read-only window", ram[0 * 8192 + 1], 0x00); (*total)++;
        /* SCSI: 하드 디스크 (ID 0) */
        scsi_img = img;
        ide_attach(&m.ide, 64, scsi_rd, scsi_wr, 0);
        FW(0x6000, 0x7F);                                   /* 0x4000 창 = SPC */
        #define SPCW(r, v) FW(0x5FF0 + (r), (v))
        #define SPCR(r) m.cpu.mem_read(m.cpu.ctx, (uint16_t)(0x5FF0 + (r)))
        #define DR() m.cpu.mem_read(m.cpu.ctx, 0x4000)
        SPCW(1, 0x00); SPCW(0, 7);                          /* SCTL = 0 (켬), 내 ID 7 */
        SPCW(4, 0xFF);
        SPCW(11, 0x81);                                     /* 고를 장치: ID 7(나) + ID 0 */
        SPCW(2, 0x20);                                      /* Select */
        f += expect("SPC select complete", (SPCR(4) & 0x10) != 0, 1); (*total)++;
        f += expect("SPC command phase", SPCR(5) & 0xCF, 0x8A); (*total)++;
        SPCW(4, 0xFF);
        SPCW(8, 2); SPCW(14, 6); SPCW(13, 0); SPCW(12, 0); SPCW(2, 0x80);
        { static const uint8_t inq[6] = { 0x12, 0, 0, 0, 36, 0 }; for (int i = 0; i < 6; i++) FW(0x4000, inq[i]); }
        f += expect("SPC INQUIRY data phase", SPCR(5) & 0x07, 0x01); (*total)++;
        SPCW(8, 1); SPCW(14, 36); SPCW(2, 0x80);
        uint8_t iq[36]; for (int i = 0; i < 36; i++) iq[i] = DR();
        f += expect("SPC INQUIRY type", iq[0], 0); (*total)++;
        f += expect("SPC INQUIRY vendor", memcmp(iq + 8, "X86MSX", 6), 0); (*total)++;
        SPCW(8, 3); SPCW(14, 1); SPCW(2, 0x80);
        f += expect("SPC status GOOD", DR(), 0); (*total)++;
        SPCW(8, 7); SPCW(14, 1); SPCW(2, 0x80); (void)DR();   /* 메시지 인 */
        f += expect("SPC bus free", SPCR(5) & 0x08, 0); (*total)++;
        /* READ(10) LBA 3, 2 섹터 */
        SPCW(4, 0xFF); SPCW(8, 0); SPCW(11, 0x81); SPCW(2, 0x20);
        SPCW(8, 2); SPCW(14, 10); SPCW(2, 0x80);
        { static const uint8_t rd[10] = { 0x28, 0, 0, 0, 0, 3, 0, 0, 2, 0 }; for (int i = 0; i < 10; i++) FW(0x4000, rd[i]); }
        SPCW(8, 1); SPCW(13, 0x04); SPCW(14, 0); SPCW(2, 0x80);      /* 1024 바이트 */
        int bad = 0; for (int i = 0; i < 1024; i++) if (DR() != img[3 * 512 + i]) bad++;
        f += expect("SPC READ(10) 2 sectors", bad, 0); (*total)++;
        SPCW(8, 3); SPCW(13, 0); SPCW(14, 1); SPCW(2, 0x80); (void)DR();
        SPCW(8, 7); SPCW(14, 1); SPCW(2, 0x80); (void)DR();
        /* WRITE(10) LBA 9, 1 섹터 */
        SPCW(4, 0xFF); SPCW(8, 0); SPCW(11, 0x81); SPCW(2, 0x20);
        SPCW(8, 2); SPCW(14, 10); SPCW(2, 0x80);
        { static const uint8_t wr[10] = { 0x2A, 0, 0, 0, 0, 9, 0, 0, 1, 0 }; for (int i = 0; i < 10; i++) FW(0x4000, wr[i]); }
        SPCW(8, 0); SPCW(13, 0x02); SPCW(14, 0); SPCW(2, 0x80);
        for (int i = 0; i < 512; i++) FW(0x4000, 0xC0 ^ i);
        int bw = 0; for (int i = 0; i < 512; i++) if (img[9 * 512 + i] != (uint8_t)(0xC0 ^ i)) bw++;
        f += expect("SPC WRITE(10) 1 sector", bw, 0); (*total)++;
        SPCW(8, 3); SPCW(13, 0); SPCW(14, 1); SPCW(2, 0x80);
        f += expect("SPC write status GOOD", DR(), 0); (*total)++;
        SPCW(8, 7); SPCW(14, 1); SPCW(2, 0x80); (void)DR();
        /* 없는 ID 5 고르기 → 시간 초과 */
        SPCW(4, 0xFF); SPCW(8, 0); SPCW(11, 0xA0); SPCW(2, 0x20);
        f += expect("SPC select timeout", (SPCR(4) & 0x04) != 0, 1); (*total)++;
        msx_host_free(&m);
        memset(ram, 0xFF, sizeof ram);
        for (int i = 0; i < 16; i++) ram[i * 8192] = (uint8_t)i;
        if (msx_host_init(&m, profile, err, sizeof err)) return 1;
        msx_insert_cart(&m, 0, ram, sizeof ram, MAPPER_WAVESCSI);
        msx_reset(&m);
        s = m.cart_slot[0];
        msx_io_write(&m, 0xA8, (uint8_t)((s << 2) | (s << 4)));
        f += expect("WAVESCSI reset bank", msx_peek(&m, 0x4000), 0); (*total)++;
        FW(0x7000, 2);
        f += expect("WAVESCSI bank 0x6000", msx_peek(&m, 0x6000), 2); (*total)++;
        FW(0x7FFE, 0x40);                                   /* SPC 켜짐 (쓰기 허용 꺼짐) */
        scsi_img = img;
        ide_attach(&m.ide, 64, scsi_rd, scsi_wr, 0);
        FW(0x5FF1, 0x00); FW(0x5FF0, 7); FW(0x5FF4, 0xFF); FW(0x5FFB, 0x81); FW(0x5FF2, 0x20);
        f += expect("WAVESCSI SPC select", (m.cpu.mem_read(m.cpu.ctx, 0x5FF4) & 0x10) != 0, 1); (*total)++;
        FW(0x7FFE, 0x10);                                   /* SPC 꺼짐, 쓰기 허용 */
        f += expect("WAVESCSI SPC off -> SRAM", msx_peek(&m, 0x4000), 0); (*total)++;
        FW(0x4123, 0x5A);
        f += expect("WAVESCSI SRAM write", ram[0x123], 0x5A); (*total)++;
        FW(0x7FFE, 0x00);
        FW(0x9000, 0x3F);                                   /* SCC 켜기 */
        FW(0x9800, 0x55);
        f += expect("WAVESCSI SCC wave ram", m.cpu.mem_read(m.cpu.ctx, 0x9800), 0x55); (*total)++;
        msx_host_free(&m);
    }
    return f;
}

int main(int argc, char **argv)
{
    const char *profile = argc > 1 ? argv[1] : "machines/DPC-200";
    const char *dir = "tests/carts";
    char path[512], line[512], err[256];
    snprintf(path, sizeof path, "%s/expected.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f) { printf("missing %s (run tests/carts/make_carts.py)\n", path); return 1; }
    int fails = 0, total = 0;
    while (fgets(line, sizeof line, f)) {
        char *name = strtok(line, "\t"), *mapper = strtok(NULL, "\t"), *text = strtok(NULL, "\r\n");
        if (!name || !mapper || !text) continue;
        snprintf(path, sizeof path, "%s/%s", dir, name);
        FILE *rf = fopen(path, "rb");
        if (!rf) { printf("FAIL %s: cannot open\n", name); fails++; continue; }
        static uint8_t rom[MSX_ROM_MAX];
        uint32_t size = (uint32_t)fread(rom, 1, sizeof rom, rf);
        fclose(rf);

        MapperType guess = cart_guess_mapper(rom, size);
        MapperType want = MAPPER_PLAIN;
        for (int t = 0; t < MAPPER_COUNT; t++)
            if (!strcmp(cart_mapper_name((MapperType)t), mapper)) want = (MapperType)t;
        total++;
        int ok_guess = guess == want;
        if (!ok_guess) fails++;
        printf("%s %-14s guess %-9s (want %s)\n", ok_guess ? "PASS" : "FAIL", name, cart_mapper_name(guess), mapper);

        if (msx_host_init(&m, profile, err, sizeof err)) { printf("init: %s\n", err); return 1; }
        msx_insert_cart(&m, 0, rom, size, want);
        msx_reset(&m);
        for (int fr = 0; fr < 600; fr++) msx_run_frame(&m);
        total++;
        int ok_run = screen_has(text);
        if (!ok_run) fails++;
        printf("%s %-14s run   \"%s\"\n", ok_run ? "PASS" : "FAIL", name, text);
        msx_host_free(&m);
    }
    fclose(f);
    fails += mapper_tests(profile, &total);
    printf("carttest: %d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
