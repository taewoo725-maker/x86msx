/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* sc55_all.c - 5.2.60: Nuked-SC55 (Roland SC-55 실기 롬 에뮬레이션, nukeykt, GPL-2.0-or-later, third_party/nukedsc55) 를 한 번에 빌드하는 감싸개.
 * nsc_shim.h 가 printf 를 없애고 전역 이름에 nsc_ 를 붙임 (X86MSX 의 ram·timer 등과 안 겹치게) */
#pragma GCC diagnostic ignored "-Wpragmas"
#pragma GCC diagnostic ignored "-Wunknown-warning-option"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-braces"
#pragma GCC diagnostic ignored "-Wreturn-type"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wshift-negative-value"
#pragma GCC diagnostic ignored "-Wbitwise-instead-of-logical"
#if defined(__i386__)
/* 레거시(BIOS, 32비트) 본체는 BSS 자리(memmap.h)가 모자라 SC-55 를 빼고 빈 함수만 (MT-32 처럼 UEFI 판에서만) */
#include "../third_party/nukedsc55/sc55w.h"
const char *const sc55w_files[2][SC55_NFILE] = { { "rom1.bin", "rom2.bin", "waverom1.bin", "waverom2.bin", "rom_sm.bin" }, { "sc55_rom1.bin", "sc55_rom2.bin", "sc55_waverom1.bin", "sc55_waverom2.bin", "sc55_waverom3.bin" } };
uint8_t *sc55w_tmp(void) { return 0; }
int  sc55w_load(int m, int i, uint32_t n) { (void)m; (void)i; (void)n; return -1; }
int  sc55w_start(int m) { (void)m; return -1; }
void sc55w_close(void) {}
int  sc55w_ready(void) { return 0; }
int  sc55w_model(void) { return 0; }
void sc55w_midi(uint8_t b) { (void)b; }
void sc55w_reset(void) {}
void sc55w_render(int32_t *l, int32_t *r, int n) { for (int i = 0; i < n; i++) l[i] = r[i] = 0; }
void sc55w_lcd_text(char *o) { o[0] = 0; }
void sc55w_lcd_part(char *o) { o[0] = 0; }
void sc55w_lcd_dots(uint16_t d[16]) { for (int i = 0; i < 16; i++) d[i] = 0; }
uint32_t sc55w_steps(void) { return 0; }
#else
#include "../third_party/nukedsc55/nsc_shim.h"
#include "../third_party/nukedsc55/mcu.c"
#include "../third_party/nukedsc55/mcu_opcodes.c"
#include "../third_party/nukedsc55/mcu_interrupt.c"
#include "../third_party/nukedsc55/mcu_timer.c"
#include "../third_party/nukedsc55/pcm.c"
#include "../third_party/nukedsc55/submcu.c"
#include "../third_party/nukedsc55/lcd.c"
#include "../third_party/nukedsc55/sc55w.c"
#endif
