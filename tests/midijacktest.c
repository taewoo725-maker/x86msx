/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* midijacktest.c - 5.2.56: MSX 쪽 MIDI 단자 — SFG YM2148(3FF3/3FF5/3FF6, 보내기·받기·IRQ 벡터), Music Module MC6850(포트 0/1·4/5, 프로그램들의 검사 순서), 단자 표 */
#include "../host/msx_host.h"
#include "../src/mididev.h"
#include <stdio.h>
#include <string.h>

static MSX m;
static uint8_t sfgrom[16384], blank[16384];
static int f, t;
static void expect(const char *what, int got, int want) { printf("%s %-52s %02X (want %02X)\n", got == want ? "PASS" : "FAIL", what, got, want); t++; if (got != want) f++; }
static uint8_t rd(uint16_t a) { return m.cpu.mem_read(m.cpu.ctx, a); }
static void wr(uint16_t a, uint8_t v) { m.cpu.mem_write(m.cpu.ctx, a, v); }

int main(int argc, char **argv)
{
    char err[256];
    if (msx_host_init(&m, argc > 1 ? argv[1] : "machines/DPC-200", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    expect("no jacks on plain MSX1", msx_midi_jacks(&m), 0);
    /* ---- SFG ---- */
    memset(sfgrom, 0xC9, sizeof sfgrom);
    expect("insert SFG", msx_insert_cart(&m, 0, sfgrom, sizeof sfgrom, MAPPER_SFG), 0);
    msx_reset(&m); msx_run_frame(&m);
    expect("jack SFG", msx_midi_jacks(&m), 1 << MDP_SFG);
    int s = m.cart_slot[0]; uint8_t a8 = m.primary;
    msx_io_write(&m, 0xA8, (uint8_t)((a8 & 0xFC) | s));          /* 0000~3FFF = SFG */
    expect("status after reset (TxEN off)", rd(0x3FF6), 0x00);
    wr(0x3FF6, 0x01); expect("TxEN -> TxRDY", rd(0x3FF6) & 1, 1);
    m.midi_n = 0; wr(0x3FF5, 0x90); wr(0x3FF5, 0x3C);
    expect("SFG sent 2 bytes", m.midi_n, 2); expect("tagged SFG", (m.midi_t[0] & MIDI_T_SRC) == MIDI_T_SFG, 1);
    wr(0x3FF3, 0x42); wr(0x3FF6, 0x0D);                         /* 벡터 42h, TxEN RxEN RxIE */
    uint8_t in[2] = { 0xF8, 0x55 };
    expect("midi_in_to SFG", msx_midi_in_to(&m, MDP_SFG, in, 2), 2);
    expect("RxRDY", rd(0x3FF6) & 2, 2);
    expect("IRQ_SFGMIDI line", (int)(m.irq_lines >> IRQ_SFGMIDI) & 1, 1);
    expect("IM2 vector = 3FF3h value", m.cpu.int_vec, 0x42);
    expect("read data F8", rd(0x3FF5), 0xF8);
    expect("RxRDY cleared, IRQ off", ((rd(0x3FF6) & 2) | ((int)(m.irq_lines >> IRQ_SFGMIDI) & 1)), 0);
    msx_io_write(&m, 0xA8, a8); msx_run_frame(&m); msx_io_write(&m, 0xA8, (uint8_t)((a8 & 0xFC) | s));
    expect("second byte after 320 us", rd(0x3FF5), 0x55);
    wr(0x3FF6, 0x80); expect("internal reset", rd(0x3FF6), 0x00);
    msx_io_write(&m, 0xA8, a8);
    msx_eject_cart(&m, 0); msx_run_frame(&m);
    /* ---- Music Module MC6850 ---- */
    memset(blank, 0xFF, sizeof blank);
    msx_insert_cart(&m, 0, blank, sizeof blank, MAPPER_PLAIN); msx_cart_set_uart(&m, 0, UART_MC6850);
    msx_reset(&m); msx_run_frame(&m);
    expect("jack Music Module", msx_midi_jacks(&m), 1 << MDP_MM);
    expect("tetris 2: INP(4) = 0 after reset", msx_io_read(&m, 4), 0);
    msx_io_write(&m, 0, 3); msx_io_write(&m, 0, 21);
    expect("fac demo 5: OUT 0,3 : OUT 0,21 : INP(4) = 2", msx_io_read(&m, 4), 2);
    m.midi_n = 0; msx_io_write(&m, 5, 0xFE);
    expect("Synthesix: OUT 5,FE -> bit1 0 (sending)", msx_io_read(&m, 4) & 2, 0);
    expect("byte went out on MM jack", m.midi_n == 1 && (m.midi_t[0] & MIDI_T_SRC) == MIDI_T_MM, 1);
    msx_run_frame(&m); expect("TDRE back after 320 us", msx_io_read(&m, 4) & 2, 2);
    msx_io_write(&m, 0, 3); expect("master reset -> 0", msx_io_read(&m, 4), 0);
    msx_io_write(&m, 0, 0x95);                                   /* /16, 8N1, RIE */
    expect("midi_in_to MM", msx_midi_in_to(&m, MDP_MM, in + 1, 1), 1);
    msx_run_frame(&m);
    expect("RDRF + IRQ bit", msx_io_read(&m, 4) & 0x81, 0x81);
    expect("IRQ_MMMIDI line", (int)(m.irq_lines >> IRQ_MMMIDI) & 1, 1);
    expect("read data 55", msx_io_read(&m, 5), 0x55);
    expect("IRQ released", (int)(m.irq_lines >> IRQ_MMMIDI) & 1, 0);
    msx_eject_cart(&m, 0); msx_run_frame(&m);
    expect("ejected: port 4 not ours", msx_io_read(&m, 4) == 0xFF, 1);
    printf("midijacktest: %d/%d passed\n", t - f, t);
    return f != 0;
}
