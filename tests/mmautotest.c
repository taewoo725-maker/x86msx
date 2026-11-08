/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mmautotest.c - 8.0.226: X86MV·X86MP3 가 소리 포트 2Ch~2Fh 를 건드렸는데 X86 Multimedia 가 없을 때 호스트에게 꽂을 기회를 주는 연결부 (msx_set_mm_probe)
 *   1) 호스트가 정하지 않음: 2Ch = FFh, 아무 일도 없음
 *   2) 호스트가 못 꽂음(0): 계속 FFh, 접근마다 물어봄
 *   3) 호스트가 꽂음(1): 바로 그 읽기부터 카트리지가 답함 (상태 비트 7), 이후엔 묻지 않음
 *   4) 쓰기가 먼저여도 같음 (IDENT 쓰기 → 'M','V','S',01h)
 *   5) 카트리지를 빼면 다시 물어봄 */
#include "../host/msx_host.h"
#include <stdio.h>
#include <string.h>

static MSX m;
static uint8_t blank[16384];
static int asked, host_can_insert;

static int host_probe(MSX *machine)
{
    asked++;
    if (!host_can_insert) return 0;
    return msx_insert_cart(machine, 1, blank, sizeof blank, MAPPER_MMEDIA) == 0;
}
static int expect(const char *what, long got, long want)
{
    printf("%s %-56s %ld (want %ld)\n", got == want ? "PASS" : "FAIL", what, got, want);
    return got != want;
}

int main(int argc, char **argv)
{
    char err[256]; int f = 0;
    if (msx_host_init(&m, argc > 1 ? argv[1] : "machines/DPC-200", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    memset(blank, 0xFF, sizeof blank);

    f += expect("no host hook: 2Ch = FF", msx_io_read(&m, 0x2C), 0xFF);
    f += expect("no host hook: nothing asked", asked, 0);

    msx_set_mm_probe(host_probe); msx_io_rebuild(&m);
    host_can_insert = 0;
    f += expect("host cannot insert: 2Ch = FF", msx_io_read(&m, 0x2C), 0xFF);
    f += expect("host cannot insert: 2Dh = FF", msx_io_read(&m, 0x2D), 0xFF);
    f += expect("host cannot insert: asked each access", asked, 2);
    msx_io_write(&m, 0x2C, 0x00);
    f += expect("host cannot insert: write also asked", asked, 3);

    host_can_insert = 1; asked = 0;
    f += expect("host inserts: status bit7 on first read", (msx_io_read(&m, 0x2C) >> 7) & 1, 1);
    f += expect("host inserts: asked once", asked, 1);
    msx_io_write(&m, 0x2C, 0x00);
    f += expect("IDENT 1 = 'M'", msx_io_read(&m, 0x2D), 'M');
    f += expect("IDENT 2 = 'V'", msx_io_read(&m, 0x2D), 'V');
    f += expect("cart present: not asked again", asked, 1);

    msx_eject_cart(&m, 1);
    host_can_insert = 0; asked = 0;
    f += expect("after eject: 2Ch = FF again", msx_io_read(&m, 0x2C), 0xFF);
    f += expect("after eject: asked again", asked, 1);

    host_can_insert = 1; asked = 0;
    msx_io_write(&m, 0x2C, 0x00);
    f += expect("write first: IDENT 1 = 'M'", msx_io_read(&m, 0x2D), 'M');
    f += expect("write first: IDENT 2 = 'V'", msx_io_read(&m, 0x2D), 'V');
    f += expect("write first: IDENT 3 = 'S'", msx_io_read(&m, 0x2D), 'S');
    f += expect("write first: IDENT 4 = 01", msx_io_read(&m, 0x2D), 0x01);

    printf(f ? "FAIL %d\n" : "ALL OK\n", f);
    return f != 0;
}
