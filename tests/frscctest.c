/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* frscctest.c - 5.2.47: popolon Flash-ROM SCC (Developer Edition): M29W128 16 MiB, Konami SCC 매퍼 + 오프셋 + 16비트 레지스터, SCC + DAC */
#include "../host/msx_host.h"
#include "../src/persist.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static MSX m;
static uint8_t *buf;
static int f, t;
static void expect(const char *what, int got, int want) { printf("%s %-46s %02X (want %02X)\n", got == want ? "PASS" : "FAIL", what, got, want); t++; if (got != want) f++; }
static uint8_t rd(uint16_t a) { return m.cpu.mem_read(m.cpu.ctx, a); }
static void wr(uint16_t a, uint8_t v) { m.cpu.mem_write(m.cpu.ctx, a, v); }
static void cmd(uint8_t c) { wr(0x4AAA, 0xAA); wr(0x4555, 0x55); wr(0x4AAA, c); }   /* 16 MB 판: 1st 4AAAh, 2nd 4555h */

int main(void)
{
    char err[256];
    buf = malloc(16u << 20);
    if (msx_host_init(&m, "tests/regress/local/machines/dpc200", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    for (uint32_t i = 0; i < (16u << 20); i++) buf[i] = (uint8_t)((i >> 13) ^ (i >> 21));   /* 8 KiB 블록 번호 (아래 8비트) xor 위 */
    msx_insert_cart(&m, 0, buf, 16u << 20, MAPPER_FRSCC);
    msx_reset(&m);
    int s = m.cart_slot[0];
    msx_io_write(&m, 0xA8, (uint8_t)(s | (s << 2) | (s << 4)));          /* 0000~BFFF = 카트리지 슬롯 (3800h 레지스터) */
    Cartridge *c = &m.cart[0];
    expect("reset: 4000 = block 0", rd(0x4000), 0); expect("reset: A000 = block 3", rd(0xA000), 3);
    expect("3800~3FFF reads FF (write only)", rd(0x3900) == 0xFF, 1);
    wr(0x5000, 10); wr(0x7000, 11); wr(0x9000, 12); wr(0xB000, 13);
    expect("konami scc: 4000", rd(0x4000), 10); expect("konami scc: 6000", rd(0x6000), 11); expect("konami scc: 8000", rd(0x8000), 12); expect("konami scc: A000", rd(0xA000), 13);
    wr(0x57FF, 20); expect("mirror 57FF", rd(0x4000), 20); wr(0x5800, 21); expect("5800 is not a register", rd(0x4000), 20);
    /* 오프셋 3800/3801 (8 KiB 단위, 모든 뱅크에) */
    wr(0x3800, 0x00); wr(0x3801, 0x01);                                   /* 256 → 2 MiB 뒤 (블록 256 → 값 (256+20)&0xFF ^ 1) */
    expect("offset 256: 4000 = block 276", rd(0x4000), (276 & 0xFF) ^ (276 >> 8));
    expect("offset 256: 6000 = block 267", rd(0x6000), (267 & 0xFF) ^ (267 >> 8));
    wr(0x3FFA, 0x05); expect("3FFA mirrors 3800 (L, A2=0)", c->xbank[4], 0x0105);
    wr(0x3800, 0); wr(0x3801, 0);
    /* 16비트 모드 */
    wr(0x3804, 0x02); wr(0x5000, 0x34); wr(0x5001, 0x12);
    expect("16-bit bank reg", c->xbank[0], 0x1234); expect("16-bit: 4000 = block 0x1234 & 0x7FF", rd(0x4000), ((0x1234 & 0x7FF) & 0xFF) ^ ((0x1234 & 0x7FF) >> 8));
    wr(0x3804, 0x00); wr(0x5000, 7); expect("8-bit mode again", c->xbank[0], 7);
    /* 플래시: ID 표 (popolon 문서), 쓰기, 128 KiB 섹터 지우기 */
    wr(0x5000, 0); wr(0x7000, 1); wr(0x9000, 2); wr(0xB000, 3);
    cmd(0x90);
    expect("autoselect 4000 = 20", rd(0x4000), 0x20); expect("4001 = 20", rd(0x4001), 0x20); expect("4002 = 7E", rd(0x4002), 0x7E);
    expect("4006 = 19", rd(0x4006), 0x19); expect("401C = 21", rd(0x401C), 0x21); expect("401E = 01", rd(0x401E), 0x01); expect("4004 = 00", rd(0x4004), 0x00);
    wr(0x4000, 0xF0); expect("F0: data again", rd(0x4000), 0);
    cmd(0xA0); wr(0x6100, 0x00); expect("program 6100 = 00", rd(0x6100), 0x00); expect("dirty", c->sram_dirty, 1);
    expect("dirty map block 1", c->fl_dirty[0] & 2, 2);
    wr(0x7000, 16); cmd(0xA0); wr(0x6000, 0x00); expect("program block 16 = 00", rd(0x6000), 0x00);
    wr(0x7000, 1);
    wr(0x4AAA, 0xAA); wr(0x4555, 0x55); wr(0x4AAA, 0x80); wr(0x4AAA, 0xAA); wr(0x4555, 0x55); wr(0x6100, 0x30);   /* 섹터 0 (0~1FFFF) 지우기 */
    expect("sector erase: 6100 = FF", rd(0x6100), 0xFF); expect("sector erase: 4000 = FF", rd(0x4000), 0xFF);
    wr(0x7000, 16); expect("block 16 (next sector) untouched", rd(0x6000), 0x00); wr(0x7000, 1);
    /* SCC + DAC */
    wr(0x9000, 0x3F); wr(0x9800, 0x55); expect("SCC wave", rd(0x9800), 0x55);
    m.pcm_n = 0; wr(0x98FC, 0x80); expect("DAC_Data 98FC", m.pcm_n, 1); expect("DAC value", m.pcm_da, 0x80);
    expect("Knm DAC off: 4000 write is flash cmd (no DAC)", (wr(0x4000, 0x40), m.pcm_n), 1);
    wr(0x98FB, 0x10); wr(0x4123, 0x7F); expect("Knm DAC on: 4000~4FFF write → DAC", m.pcm_n, 2); expect("Knm DAC value", m.pcm_da, 0x7F);
    wr(0x5000, 0); expect("bank reg still works with Knm DAC", c->xbank[0], 0);
    wr(0x98FB, 0x00);
    expect("PCM channel present (cart DAC)", msx_ch_present(&m, MSX_CH_PCM), 1);
    /* 영속 */
    { MsxPersist p; expect("persist", msx_cart_persist(&m, 0, 0, 1, &p), 1); expect("persist name", strcmp(p.name, "FlashROM_SCC.flash"), 0); expect("persist fmt FLASH", p.fmt, PS_FLASH); expect("persist size 16 MiB", p.size == (16u << 20), 1); }
    msx_reset(&m); expect("reset: offset 0", c->xbank[4], 0); expect("reset: mode 0", c->y_en, 0); expect("reset: bank 3", c->xbank[3], 3);
    printf("frscctest: %d/%d passed\n", t - f, t);
    return f != 0;
}
