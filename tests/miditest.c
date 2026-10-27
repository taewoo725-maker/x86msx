/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* miditest.c - 3.4.4 DEV-35: MSX-MIDI 8251 받기, 8253 카운터·타이머 IRQ (기종에 MSX-MIDI가 없으면 prof.midi를 켜서) */
#include "../host/msx_host.h"
#include <stdio.h>
#include <string.h>

static MSX m;
static int expect(const char *what, int got, int want)
{
    printf("%s %-44s %d (want %d)\n", got == want ? "PASS" : "FAIL", what, got, want);
    return got != want;
}
static void run_cycles(uint32_t n) { uint64_t t = m.cpu.cycles + n; while (m.cpu.cycles < t) msx_run_frame(&m); }

int main(int argc, char **argv)
{
    char err[256];
    int f = 0, t = 0;
    if (msx_host_init(&m, argc > 1 ? argv[1] : "machines/DPC-200", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    m.prof.midi = 1;
    msx_io_rebuild(&m);                                 /* 5.2.9 */
    msx_reset(&m);
    for (int i = 0; i < 30; i++) msx_run_frame(&m);
    msx_io_write(&m, 0xE9, 0x4E);                       /* 모드 (비동기 ×16, 8비트) */
    msx_io_write(&m, 0xE9, 0x05);                       /* TxEN RxE, IRQ 둘 다 막음 */
    /* 카운터 2: 모드 2, 40000 (4 MHz → 10 ms) */
    msx_io_write(&m, 0xEF, 0xB4);
    msx_io_write(&m, 0xEE, 0x40); msx_io_write(&m, 0xEE, 0x9C);
    msx_io_write(&m, 0xEF, 0x80);                       /* 카운터 2 붙잡기 */
    int c = msx_io_read(&m, 0xEE); c |= msx_io_read(&m, 0xEE) << 8;
    f += expect("8253 counter 2 latched near 40000", c > 39000 && c <= 40000, 1); t++;
    f += expect("timer flag before 10 ms", (msx_io_read(&m, 0xE9) >> 7) & 1, 0); t++;
    run_cycles(36000 + 500);                            /* 10 ms 남짓 */
    f += expect("timer flag (DSR) after 10 ms", (msx_io_read(&m, 0xE9) >> 7) & 1, 1); t++;
    msx_io_read(&m, 0x99);
    msx_io_write(&m, 0xE9, 0x05);
    f += expect("timer IRQ masked without DTR", m.cpu.int_line, 0); t++;
    msx_io_write(&m, 0xE9, 0x07);                       /* DTR = 타이머 IRQ 허용 */
    f += expect("timer IRQ with DTR", m.cpu.int_line, 1); t++;
    msx_io_write(&m, 0xEA, 0);                          /* 깃발 지움 */
    msx_io_read(&m, 0x99); msx_io_write(&m, 0xE9, 0x07);
    f += expect("EAh clears timer IRQ", m.cpu.int_line, 0); t++;
    run_cycles(36000);
    f += expect("periodic: flag again after 10 ms", (msx_io_read(&m, 0xE9) >> 7) & 1, 1); t++;
    msx_io_write(&m, 0xEA, 0);
    msx_io_write(&m, 0xE9, 0x05);                       /* 타이머 IRQ 끔 */
    /* 받기 */
    static const uint8_t note[3] = { 0x90, 0x3C, 0x64 };
    f += expect("msx_midi_in accepts 3", msx_midi_in(&m, note, 3), 3); t++;
    run_cycles(2000);
    f += expect("RxRDY", (msx_io_read(&m, 0xE9) >> 1) & 1, 1); t++;
    f += expect("byte 1 = 90h", msx_io_read(&m, 0xE8), 0x90); t++;
    run_cycles(1200);
    msx_io_write(&m, 0xE9, 0x25);                       /* RTS = 받기 IRQ 허용 */
    msx_io_read(&m, 0x99); msx_io_write(&m, 0xE9, 0x25);
    f += expect("RxRDY IRQ with RTS", m.cpu.int_line, 1); t++;
    f += expect("byte 2 = 3Ch", msx_io_read(&m, 0xE8), 0x3C); t++;
    run_cycles(1200);
    f += expect("byte 3 = 64h", msx_io_read(&m, 0xE8), 0x64); t++;
    f += expect("RxRDY cleared after last byte", (msx_io_read(&m, 0xE9) >> 1) & 1, 0); t++;
    msx_io_write(&m, 0xE9, 0x01);                       /* 받기 끔: 들어온 바이트는 기다림 */
    msx_midi_in(&m, note, 1);
    run_cycles(3000);
    f += expect("RxE off: no RxRDY", (msx_io_read(&m, 0xE9) >> 1) & 1, 0); t++;
    printf("miditest: %d/%d passed\n", t - f, t);
    return f != 0;
}
