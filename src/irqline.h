/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * irqline.h - 인터럽트선 (구조 개편 1단계, 5.2.0) — 뼈대: 바꾸지 않음
 *
 * 장치마다 선 하나(비트). 장치는 자기 선만 올리고 내린다 (msx_irq_set). CPU INT 핀 = 모든 선의 OR.
 * 데이터 버스에 벡터를 싣는 장치(IM 2: SFG 등)는 msx_irq_vector 로 알린다.
 */
#ifndef IRQLINE_H
#define IRQLINE_H
enum IrqLine {             /* 새 장치는 끝에 더함 */
    IRQ_VDP = 0,           /* TMS9918/V99x8: 수직 귀선·줄 일치 */
    IRQ_V9990,             /* Gfx9000 */
    IRQ_OPM,               /* SFG (YM2151/YM2164) 타이머 — IM 2 벡터를 몬다 */
    IRQ_OPL4,              /* MoonSound FM 타이머 */
    IRQ_MIDI,              /* MSX-MIDI (8251·8253) */
    IRQ_NET,               /* 네트워크 카트리지 */
    IRQ_RS232,             /* RS-232C 카트리지 (8251 RxRDY·8253 타이머) */
    IRQ_SFGMIDI,           /* SFG 의 YM2148 MIDI UART (벡터 = 3FF3h 에 쓴 값) */
    IRQ_MMMIDI,            /* Music Module 의 MC6850 MIDI UART */
    IRQ_COUNT
};
#endif
