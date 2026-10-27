/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* msirqtest.c - 3.4.4: MoonSound(OPL4) FM 타이머 IRQ가 CPU /INT에 이어지는지, 웨이브 RAM 자리 */
#include "../host/msx_host.h"
#include <stdio.h>
#include <string.h>

static MSX m;
static uint8_t wave[OPL4_MEM_SIZE];

static int expect(const char *what, int got, int want)
{
    printf("%s %-44s %d (want %d)\n", got == want ? "PASS" : "FAIL", what, got, want);
    return got != want;
}

int main(int argc, char **argv)
{
    char err[256];
    int f = 0, t = 0;
    opl4_set_memory(wave, 1);                           /* 롬 있음 표시만 (내용 0) */
    if (msx_host_init(&m, argc > 1 ? argv[1] : "machines/DPC-200", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    msx_moonsound_enable(&m, 1);
    msx_reset(&m);
    for (int i = 0; i < 60; i++) msx_run_frame(&m);
    f += expect("MoonSound on", m.ms_on, 1); t++;
    msx_io_write(&m, 0xC4, 0x02); msx_io_write(&m, 0xC5, 0x00);   /* 타이머 1 = 256 × 80 µs ≈ 20 ms */
    msx_io_write(&m, 0xC4, 0x04); msx_io_write(&m, 0xC5, 0x60);   /* 둘 다 가림 → IRQ 없음 */
    msx_io_write(&m, 0xC5, 0x01 | 0x40);
    for (int i = 0; i < 3; i++) msx_run_frame(&m);
    f += expect("masked timer: no IRQ flag", m.opl4.status & 0x80, 0); t++;
    msx_io_write(&m, 0xC4, 0x04); msx_io_write(&m, 0xC5, 0x01);   /* 타이머 1 시작, 가리지 않음 */
    for (int i = 0; i < 3; i++) msx_run_frame(&m);
    f += expect("timer 1 IRQ flag (status bit 7|6)", m.opl4.status & 0xC0, 0xC0); t++;
    msx_io_read(&m, 0x99);                              /* VDP IRQ 지우기 */
    msx_io_write(&m, 0xC4, 0x04);
    f += expect("CPU /INT held by OPL4", m.cpu.int_line, 1); t++;
    msx_io_write(&m, 0xC5, 0x80);                       /* IRQ 지우기 (타이머도 멈춤 아님: 비트 0은 0) */
    msx_io_read(&m, 0x99);
    msx_io_write(&m, 0xC4, 0x04);
    f += expect("IRQ reset releases /INT", m.cpu.int_line, 0); t++;
    printf("msirqtest: %d/%d passed\n", t - f, t);
    return f != 0;
}
