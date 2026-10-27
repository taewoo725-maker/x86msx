/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* hbi55test.c - 5.2.45: Sony HBI-55 데이터 카트리지 (i8255 B0h~B3h + SRAM 4 KiB) — map.grauw.nl/resources/hbi55.php 의 읽기·쓰기 순서, 영속 선언 */
#include "../host/msx_host.h"
#include "../src/persist.h"
#include <stdio.h>
#include <string.h>

static MSX m;
static uint8_t blank[16384];

static int expect(const char *what, int got, int want)
{
    printf("%s %-44s %d (want %d)\n", got == want ? "PASS" : "FAIL", what, got, want);
    return got != want;
}
static void wr(uint16_t a, uint8_t d)                       /* 쓰기 순서: 80h, 자료, 주소 아래, 01xxxxxx, 00xxxxxx */
{
    msx_io_write(&m, 0xB3, 0x80); msx_io_write(&m, 0xB2, d); msx_io_write(&m, 0xB0, (uint8_t)a);
    msx_io_write(&m, 0xB1, (uint8_t)(0x40 | (a >> 8))); msx_io_write(&m, 0xB1, (uint8_t)(a >> 8));
}
static uint8_t rd(uint16_t a)                               /* 읽기 순서: 89h, 주소 아래, 11xxxxxx, IN, 10xxxxxx */
{
    msx_io_write(&m, 0xB3, 0x89); msx_io_write(&m, 0xB0, (uint8_t)a); msx_io_write(&m, 0xB1, (uint8_t)(0xC0 | (a >> 8)));
    uint8_t v = msx_io_read(&m, 0xB2); msx_io_write(&m, 0xB1, (uint8_t)(0x80 | (a >> 8))); return v;
}

int main(int argc, char **argv)
{
    char err[256]; int f = 0, t = 0;
    if (msx_host_init(&m, argc > 1 ? argv[1] : "machines/DPC-200", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    f += expect("no cart: B2h reads FF", msx_io_read(&m, 0xB2), 0xFF); t++;
    memset(blank, 0xFF, sizeof blank);
    f += expect("insert HBI-55", msx_insert_cart(&m, 1, blank, sizeof blank, MAPPER_HBI55), 0); t++;
    msx_reset(&m);
    Cartridge *c = &m.cart[1];
    memset(c->sram, 0, 4096);
    wr(0x0000, 0x12); wr(0x0FFF, 0x34); wr(0x0ABC, 0x56);
    f += expect("write 0000", c->sram[0x000], 0x12); t++;
    f += expect("write 0FFF", c->sram[0xFFF], 0x34); t++;
    f += expect("write 0ABC", c->sram[0xABC], 0x56); t++;
    f += expect("dirty", c->sram_dirty, 1); t++;
    f += expect("read 0000", rd(0x0000), 0x12); t++;
    f += expect("read 0FFF", rd(0x0FFF), 0x34); t++;
    f += expect("read 0ABC", rd(0x0ABC), 0x56); t++;
    f += expect("read 0001 (untouched)", rd(0x0001), 0x00); t++;
    /* CE 없이 읽으면 FF, OE 없이(쓰기 모드) 읽어도 FF */
    msx_io_write(&m, 0xB3, 0x89); msx_io_write(&m, 0xB0, 0); msx_io_write(&m, 0xB1, 0x80);
    f += expect("read without CE = FF", msx_io_read(&m, 0xB2), 0xFF); t++;
    /* 4000h 이상은 비트 12·13 무시 → 0000 거울 */
    msx_io_write(&m, 0xB1, 0xC0 | 0x30);
    f += expect("A12/A13 ignored (mirror)", msx_io_read(&m, 0xB2), 0x12); t++;
    /* C 가 입력 모드면 쓰기 안 됨 (실기: 합선) */
    c->sram_dirty = 0;
    msx_io_write(&m, 0xB3, 0x89); msx_io_write(&m, 0xB2, 0x99); msx_io_write(&m, 0xB0, 0x02); msx_io_write(&m, 0xB1, 0x40); msx_io_write(&m, 0xB1, 0x00);
    f += expect("no write while C is input", c->sram[2], 0x00); t++;
    f += expect("not dirty", c->sram_dirty, 0); t++;
    /* 같은 값을 다시 써도 dirty 안 됨 */
    wr(0x0000, 0x12);
    f += expect("same value: not dirty", c->sram_dirty, 0); t++;
    /* 영속 선언 */
    { MsxPersist p; int r = msx_cart_persist(&m, 1, 0, 1, &p);
      f += expect("persist declared", r, 1); t++;
      f += expect("persist name HBI-55.sram", strcmp(p.name, "HBI-55.sram"), 0); t++;
      f += expect("persist size 4096", (int)p.size, 4096); t++;
      f += expect("persist fmt RAW", p.fmt, PS_RAW); t++; }
    /* 뽑으면 포트 비어 있음 */
    msx_eject_cart(&m, 1);
    f += expect("removed: B2h reads FF", msx_io_read(&m, 0xB2), 0xFF); t++;
    printf("hbi55test: %d/%d passed\n", t - f, t);
    return f != 0;
}
