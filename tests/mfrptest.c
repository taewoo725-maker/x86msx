/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mfrptest.c - 5.2.46: Mega Flash ROM SCC+ (M29W800DB 1 MiB + SCC-I + PSG, 설정 7FFEh, 서브슬롯 FFFFh) — openMSX MegaFlashRomSCCPlus 의 규칙대로 */
#include "../host/msx_host.h"
#include "../src/persist.h"
#include "../src/msxstate.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static MSX m;
static uint8_t *buf;
static int f, t;

static void expect(const char *what, int got, int want)
{
    printf("%s %-46s %02X (want %02X)\n", got == want ? "PASS" : "FAIL", what, got, want); t++; if (got != want) f++;
}
static uint8_t rd(uint16_t a) { return m.cpu.mem_read(m.cpu.ctx, a); }
static void wr(uint16_t a, uint8_t v) { m.cpu.mem_write(m.cpu.ctx, a, v); }
static void cmd(uint8_t c) { wr(0x4555, 0xAA); wr(0x4AAA, 0x55); wr(0x4555, c); }   /* OPFX 순서 (CPU 555h/AAAh = 칩 AAAh/555h) */
static void setup(void)
{
    char err[256];
    if (msx_host_init(&m, "tests/regress/local/machines/dpc200", err, sizeof err)) { printf("init: %s\n", err); exit(1); }
    for (uint32_t i = 0; i < (1u << 20); i++) buf[i] = (uint8_t)(i >> 13);   /* 8 KiB 블록마다 번호 */
    msx_insert_cart(&m, 0, buf, 1u << 20, MAPPER_MFRSCCP);
    msx_reset(&m);
    int s = m.cart_slot[0];
    msx_io_write(&m, 0xA8, (uint8_t)((s << 2) | (s << 4) | (s << 6)));     /* 4000~FFFF = 카트리지 슬롯 */
}

int main(void)
{
    buf = malloc(1u << 20);
    setup();
    Cartridge *c = &m.cart[0];
    expect("reset: 4000 = block 0", rd(0x4000), 0); expect("reset: 6000 = block 1", rd(0x6000), 1);
    expect("reset: 8000 = block 2", rd(0x8000), 2); expect("reset: A000 = block 3", rd(0xA000), 3);
    expect("reset: 0000~3FFF not mapped (FF)", rd(0xC000), 0xFF);
    /* Konami SCC 매퍼 (기본) */
    wr(0x5000, 10); wr(0x7000, 11); wr(0x9000, 12); wr(0xB000, 13);
    expect("konami scc: 4000", rd(0x4000), 10); expect("konami scc: 6000", rd(0x6000), 11); expect("konami scc: 8000", rd(0x8000), 12); expect("konami scc: A000", rd(0xA000), 13);
    wr(0x5000, 0x80 | 2);                                                  /* 뱅크 0 레지스터에 비트 7 = 오프셋 2 */
    expect("offset 2: 4000 = 10 + 2", rd(0x4000), 12); expect("offset 2: 6000 = 11 + 2", rd(0x6000), 13);
    wr(0x5000, 0x80 | 0);
    wr(0x7000, 0x7F); expect("7 bit bank (127)", rd(0x6000), 127);
    wr(0x7FFE, 0x01); wr(0x7000, 0x7F); expect("cfg bit0: 6 bit bank (63)", rd(0x6000), 63); wr(0x7FFE, 0x00);
    /* 플래시: ID, 쓰기, 섹터 지우기 */
    wr(0x5000, 0); wr(0x7000, 1); wr(0x9000, 2); wr(0xB000, 3);
    cmd(0x90); expect("autoselect: mfr 20", rd(0x4000), 0x20); expect("autoselect: dev 5B", rd(0x4001), 0x5B);
    wr(0x4000, 0xF0); expect("F0: back to data", rd(0x4000), 0);
    cmd(0xA0); wr(0x6123, 0x00); expect("program 6123 (flash 2123) = 00", rd(0x6123), 0x00); expect("dirty", c->sram_dirty, 1);
    cmd(0xA0); wr(0x8010, 0x02 & 0x00); expect("program 8010 = 00", rd(0x8010), 0x00);
    wr(0x4555, 0xAA); wr(0x4AAA, 0x55); wr(0x4555, 0x80); wr(0x4555, 0xAA); wr(0x4AAA, 0x55); wr(0x6123, 0x30);   /* 섹터 지우기: flash 2123 → 0~3FFF (16 KiB 부트 블록) */
    expect("erase 16K boot block: 6123 = FF", rd(0x6123), 0xFF); expect("erase: 4000 = FF too", rd(0x4000), 0xFF); expect("erase: 8010 untouched", rd(0x8010), 0x00);
    wr(0x4555, 0xAA); wr(0x4AAA, 0x55); wr(0x4555, 0x80); wr(0x4555, 0xAA); wr(0x4AAA, 0x55); wr(0x8010, 0x30);   /* flash 4010 → 4000~5FFF (8 KiB) */
    expect("erase 8K block: 8010 = FF", rd(0x8010), 0xFF); expect("erase 8K: A000 (flash 6000) untouched", rd(0xA000), 3);
    /* ASCII8 */
    wr(0x7FFE, 0x80); wr(0x6000, 20); wr(0x6800, 21); wr(0x7000, 22); wr(0x7800, 23);
    expect("ascii8: 4000", rd(0x4000), 20); expect("ascii8: 6000", rd(0x6000), 21); expect("ascii8: 8000", rd(0x8000), 22); expect("ascii8: A000", rd(0xA000), 23);
    /* ASCII16 */
    wr(0x7FFE, 0xC0); wr(0x6000, 5); wr(0x7000, 9);
    expect("ascii16: 4000 = 10", rd(0x4000), 10); expect("ascii16: 6000 = 11", rd(0x6000), 11); expect("ascii16: 8000 = 18", rd(0x8000), 18); expect("ascii16: A000 = 19", rd(0xA000), 19);
    /* Konami (보통) */
    wr(0x7FFE, 0x20); wr(0x6000, 30); wr(0x8000, 31); wr(0xA000, 32); wr(0x5000, 33);
    expect("konami: 6000", rd(0x6000), 30); expect("konami: 8000", rd(0x8000), 31); expect("konami: A000", rd(0xA000), 32); expect("konami: 4000 (5000 reg ok)", rd(0x4000), 33);
    wr(0x7FFE, 0x28); wr(0x5000, 34); expect("konami cfg bit3: 4000 bank locked", rd(0x4000), 33);
    /* 64K 모드: 레지스터 = 그 칸 아무 주소, 뱅크 16 KiB */
    wr(0x7FFE, 0x40); wr(0x6000, 3); wr(0x8000, 5);
    expect("64K: 4000 = 16K bank 3 (block 6)", rd(0x4000), 6); expect("64K: 6000 = block 7", rd(0x6000), 7); expect("64K: 8000 = block 10", rd(0x8000), 10);
    expect("64K: C000 mapped (bank reg 3 = 32 → block 64)", rd(0xC000), 64);
    /* 서브슬롯 흉내 */
    wr(0x7FFE, 0x10); wr(0x5000, 3); wr(0x7000, 1); wr(0x9000, 3); wr(0xB000, 3);   /* (블록 0~2 는 위에서 지워져 FF) */
    wr(0xFFFF, 0x04);                                                      /* 페이지 1 → 서브슬롯 1 */
    expect("subslot reg read = ~04", rd(0xFFFF), 0xFB);
    expect("page1 subslot1: 4000 = block 32 (256K)", rd(0x4000), 32); expect("page2 subslot0: 8000 = block 3", rd(0x8000), 3);
    wr(0x5000, 7); expect("bank reg is per subslot: 4000 = 32 + 7", rd(0x4000), 39);
    wr(0xFFFF, 0x00); expect("back to subslot0: 4000 = block 3", rd(0x4000), 3);
    wr(0x7FFE, 0x00); expect("cfg bit4 off: FFFF not ours", rd(0xFFFF) == 0xFB, 0);
    /* SCC / SCC+ */
    wr(0x9000, 0x3F); wr(0x9800, 0x55); expect("SCC wave write/read", rd(0x9800), 0x55);
    wr(0x9800, 0xAA); expect("SCC write does not reach flash cmd", rd(0x9800), 0xAA);
    wr(0xBFFE, 0x20); wr(0xB000, 0x80); wr(0xB800, 0x66); expect("SCC+ wave write/read", rd(0xB800), 0x66);
    wr(0xBFFE, 0x00); wr(0x9000, 0);
    /* ASCII8 모드에선 SCC 없음 */
    wr(0x7FFE, 0x80); wr(0x9800, 0x11); expect("no SCC in ASCII8 mode (flash data)", rd(0x9800) == 0x11, 0); wr(0x7FFE, 0x00);
    /* 설정 레지스터 잠금 */
    wr(0x7FFE, 0x04); wr(0x7FFE, 0x80); expect("cfg locked (bit2)", c->mp_cfg, 0x04);
    /* PSG 10h/11h */
    msx_io_write(&m, 0x10, 7); msx_io_write(&m, 0x11, 0x38); expect("PSG2 reg 7 via port 10/11", m.psg2.reg[7], 0x38);
    /* 영속 선언 */
    { MsxPersist p; expect("persist", msx_cart_persist(&m, 0, 0, 1, &p), 1); expect("persist name", strcmp(p.name, "MegaFlashROM_SCCplus.flash"), 0); expect("persist size 1 MiB", p.size == (1u << 20), 1); }
    /* 상태 저장·복원 (CART 판 2) */
    { static uint8_t st[6u << 20]; c->mp_cfg = 0; wr(0x7FFE, 0x10); wr(0xFFFF, 0x04); wr(0x5000, 7);
      uint32_t n = msx_st_save(&m, st, sizeof st); expect("state save", n > 0, 1);
      wr(0xFFFF, 0); wr(0x5000, 0);
      expect("state load", msx_st_load(&m, st, n, 0, 0), 0);
      expect("state: subslot kept", c->mp_sub, 0x04); expect("state: bank kept", c->mp_bank[1][0], 7); expect("state: 4000 = 39", rd(0x4000), 39); }
    /* 리셋 */
    msx_reset(&m); expect("reset clears cfg", c->mp_cfg, 0); expect("reset banks 0..3", c->mp_bank[0][3], 3);
    printf("mfrptest: %d/%d passed\n", t - f, t);
    return f != 0;
}
