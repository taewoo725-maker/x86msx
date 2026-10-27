/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* rs232test.c - 5.2.48: MSX 표준 RS-232C 카트리지 (Philips NMS1210): 8251 80h/81h, 82h 상태·마스크, 8253 84h~87h, 받기 IRQ, 상태 저장 */
#include "../host/msx_host.h"
#include "../src/msxstate.h"
#include <stdio.h>
#include <string.h>

static MSX m;
static uint8_t blank[16384];
static int f, t;
static void expect(const char *what, int got, int want) { printf("%s %-46s %02X (want %02X)\n", got == want ? "PASS" : "FAIL", what, got, want); t++; if (got != want) f++; }
static uint8_t in(int p) { return msx_io_read(&m, (uint16_t)p); }
static void out(int p, uint8_t v) { msx_io_write(&m, (uint16_t)p, v); }

int main(int argc, char **argv)
{
    char err[256];
    if (msx_host_init(&m, argc > 1 ? argv[1] : "machines/DPC-200", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    expect("no cart: 81h = FF", in(0x81), 0xFF);
    memset(blank, 0xFF, sizeof blank);
    expect("insert", msx_insert_cart(&m, 1, blank, sizeof blank, MAPPER_RS232), 0);
    msx_reset(&m);
    for (int i = 0; i < 3; i++) msx_run_frame(&m);
    Rs232Dev *r = &m.cart[1].rs;
    expect("status 82h: CD/RI/CTS off, timer out high", in(0x82), 0xBF);
    expect("8251 status: TxRDY TxE, no RxRDY, DSR 0", in(0x81), 0x05);
    out(0x81, 0x4E);                                     /* 모드: 8N1 x16 */
    expect("mode byte stored", r->mode, 0x4E); expect("mode stage", r->mode_next, 1);
    out(0x81, 0x37);                                     /* 명령: RTS DTR RxE TxEN ERR reset */
    expect("command stored", r->cmd, 0x37);
    out(0x80, 'A'); out(0x80, 'B'); expect("tx bytes counted (TxEN)", (int)r->tx_count, 2);
    out(0x81, 0x40); expect("8251 internal reset", r->cmd, 0); expect("mode stage back", r->mode_next, 0);
    out(0x81, 0x4E); out(0x81, 0x15);                   /* RxE TxEN ERR reset, 인터럽트 마스크는 아직 FF 아님 → 기본 0: 켜짐 */
    expect("no rx yet: RxRDY 0", in(0x81) & 2, 0);
    uint8_t b[2] = { 0x55, 0xAA };
    expect("msx_rs232_in", msx_rs232_in(&m, b, 1), 1);
    msx_run_frame(&m); msx_io_read(&m, 0x99);
    expect("RxRDY after frame", in(0x81) & 2, 2);
    expect("IRQ line (mask 0 = enabled)", (int)(m.irq_lines >> IRQ_RS232) & 1, 1);
    expect("read 80h = 55", in(0x80), 0x55);
    expect("RxRDY cleared by read", in(0x81) & 2, 0);
    expect("IRQ released", (int)(m.irq_lines >> IRQ_RS232) & 1, 0);
    msx_rs232_in(&m, b + 1, 1); msx_run_frame(&m); expect("second byte", in(0x80), 0xAA); expect("rx_count 2", (int)r->rx_count, 2);
    out(0x82, 0x01);                                     /* RxRDY 인터럽트 막음 */
    msx_rs232_in(&m, b, 1); msx_run_frame(&m); msx_io_read(&m, 0x99);
    expect("masked: RxRDY set but no IRQ", (in(0x81) & 2) == 2 && !((m.irq_lines >> IRQ_RS232) & 1), 1);
    in(0x80); out(0x82, 0x00);
    /* 8253 타이머 2: 모드 0, 4 MHz 클록 → 4000 틱 = 1 ms */
    out(0x87, 0xB0); out(0x86, 0xA0); out(0x86, 0x0F);   /* 카운터 2, LSB/MSB, 모드 0, 0x0FA0 = 4000 */
    expect("timer 2 counting: 82h bit 6 = 1 (out low)", in(0x82) & 0x40, 0x40);
    msx_run_frame(&m); msx_io_read(&m, 0x99);
    expect("after 20 ms: timer flag → IRQ", (int)(m.irq_lines >> IRQ_RS232) & 1, 1);
    expect("82h bit 6 = 0 (out high)", in(0x82) & 0x40, 0);
    out(0x82, 0x08); expect("mask bit 3 clears timer IRQ", (int)(m.irq_lines >> IRQ_RS232) & 1, 0);
    out(0x87, 0x34); out(0x84, 0x0C); out(0x84, 0x00);   /* 카운터 0 모드 2, 12 → 보내기 시계 (값만 저장) */
    expect("counter 0 reload", r->pit[0].reload, 12);
    { uint8_t lo; out(0x87, 0x00); lo = in(0x84); (void)lo; expect("counter 0 latched read ok (<=12)", r->pit[0].reload >= (in(0x84) & 0x0F), 1); }
    /* 상태 저장·복원 (CART 판 3) */
    { static uint8_t st[6u << 20]; uint32_t n = msx_st_save(&m, st, sizeof st); expect("state save", n > 0, 1);
      r->cmd = 0; r->tx_count = 0;
      expect("state load", msx_st_load(&m, st, n, 0, 0), 0); expect("state: cmd kept", r->cmd, 0x15); expect("state: tx_count kept", (int)r->tx_count, 2); }
    msx_eject_cart(&m, 1);
    expect("ejected: 82h = FF", in(0x82), 0xFF);
    printf("rs232test: %d/%d passed\n", t - f, t);
    return f != 0;
}
