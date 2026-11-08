/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * bwtest.c - 네트워크 롬(unapi.rom)의 INIT 이 부팅 중에 "계속 기다릴까?"(BOOT_WAIT)를 올바로 묻는지 시험.
 * 사용법: bwtest unapi.rom N [wifi] [f2]   N번 "기다림"을 답하고 그 뒤 "부팅"을 답한다. N = -1 이면 콜백 없음.
 *   wifi=1 이면 무선 카트리지라고 답하고(안내 화면을 그려야 함), f2=1 이면 키보드 행 6 의 F2 를 누른 것으로 한다.
 * 기대: 롬이 N+1 번 물은 뒤 INIT 이 끝나고(PC=0), 첫 물음만 start=1, EXTBIO 후크가 걸려 있다.
 */
#include "../src/z80.h"
#include "../src/netcard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t mem[65536];
static NetCard nc;
static int want_wait, asks, firsts, start_seq_ok = 1, is_wifi, f2_down, setups;
static char shown[256]; static int shown_n;
static uint8_t ppi_c = 0xFF;
static int txt_on, txt_gets;
static int cb(int start) { if (asks == 0 ? !start : start) start_seq_ok = 0; if (start) firsts++; asks++; return (asks <= want_wait ? 1 : 0) | (is_wifi ? 2 : 0) | (txt_on && asks == 2 ? 4 : 0); }
static int text_cb(char *b, int max) { const char *t = "Clock: 2026-11-01 09:00:00"; (void)max; txt_gets++; strcpy(b, t); return (int)strlen(t); }
static void setup_cb(void) { setups++; }
static uint8_t mread(void *c, uint16_t a) { (void)c; return mem[a]; }
static void mwrite(void *c, uint16_t a, uint8_t v) { (void)c; if (a >= 0x4000 && a < 0x7F00) return; mem[a] = v; }
static uint64_t cyc;
static uint8_t ioread(void *c, uint16_t p) { (void)c; p &= 0xFF; if (p >= 0x28 && p <= 0x2B) return netcard_io_read(&nc, p, cyc); if (p == 0xA9) return (f2_down && (ppi_c & 0x0F) == 6) ? 0xBF : 0xFF; if (p == 0xAA) return ppi_c; return 0xFF; }
static void iowrite(void *c, uint16_t p, uint8_t v) { (void)c; p &= 0xFF; if (p >= 0x28 && p <= 0x2B) netcard_io_write(&nc, p, v, cyc); if (p == 0xAA) ppi_c = v; }

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
    if (fread(mem + 0x4000, 1, 0x4000, f) != 0x4000) return 2; fclose(f);
    want_wait = atoi(argv[2]); is_wifi = argc > 3 && atoi(argv[3]); f2_down = argc > 4 && atoi(argv[4]); txt_on = argc > 5 && atoi(argv[5]);
    netcard_init(&nc, 0x28); nc.enabled = 1;
    netcard_bootwait_cb = want_wait >= 0 ? cb : 0; netcard_bootsetup_cb = setup_cb; netcard_boottext_cb = text_cb;
    Z80 z; z80_init(&z);
    z.mem_read = mread; z.mem_write = mwrite; z.io_read = ioread; z.io_write = iowrite;
    uint16_t init = (uint16_t)(mem[0x4002] | (mem[0x4003] << 8));
    z.pc = init; z.sp.w = 0xF000; mem[0xF000] = 0; mem[0xF001] = 0;   /* 돌아갈 곳 = 0000 */
    uint64_t steps = 0;
    while (z.pc != 0) {
        if (z.pc == 0x00A2) { if (shown_n < 255) shown[shown_n++] = (char)z.af.b.h; z.pc = (uint16_t)(mem[z.sp.w] | (mem[z.sp.w + 1] << 8)); z.sp.w += 2; continue; }   /* BIOS CHPUT 흉내 */
        z80_step(&z); cyc += 4; netcard_tick(&nc, cyc);
        if (++steps > 20000000ull) { printf("bwtest: TIMEOUT (asks=%d)\n", asks); return 3; }
    }
    int hook = mem[0xFFCA] == 0xF7;
    if (z.sp.w != 0xF002) { printf("bwtest: FAIL 스택이 맞지 않음 SP=%04X\n", z.sp.w); return 1; }
    int exp_asks = want_wait < 0 ? 1 : want_wait + 1;
    if (want_wait < 0) exp_asks = 1;
    if (asks != (want_wait < 0 ? 0 : exp_asks) || (want_wait >= 0 && (firsts != 1 || !start_seq_ok)) || !hook) {
        printf("bwtest: FAIL N=%d asks=%d(기대 %d) firsts=%d seq=%d hook=%d\n", want_wait, asks, want_wait < 0 ? 0 : exp_asks, firsts, start_seq_ok, hook); return 1;
    }
    shown[shown_n] = 0;
    int banner = strstr(shown, "x86wifi") && strstr(shown, "F2");
    int want_banner = want_wait > 0 && is_wifi, want_setup = want_wait > 0 && is_wifi && f2_down;
    if ((banner != 0) != want_banner || (shown_n && !banner) || setups != (want_setup ? 1 : 0)) {
        printf("bwtest: FAIL N=%d wifi=%d f2=%d 안내=%d(기대 %d) F2설정=%d(기대 %d) 글자=[%s]\n", want_wait, is_wifi, f2_down, banner != 0, want_banner, setups, want_setup ? 1 : 0, shown); return 1;
    }
    { int want_txt = txt_on && want_wait >= 2 && is_wifi; int got_txt = strstr(shown, "Clock: 2026-11-01 09:00:00") != 0;
      if (got_txt != want_txt || txt_gets != (want_txt ? 1 : 0)) { printf("bwtest: FAIL 시계 글자 N=%d wifi=%d txt=%d 찍힘=%d(기대 %d) 가져감=%d\n", want_wait, is_wifi, txt_on, got_txt, want_txt, txt_gets); return 1; } }
    printf("bwtest: ok N=%d wifi=%d f2=%d asks=%d\n", want_wait, is_wifi, f2_down, asks);
    return 0;
}
