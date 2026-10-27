/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* midipkttest.c - 3.5.4: MIDI 바이트열 → USB-MIDI 패킷 (src/midipkt.h) */
#include "../src/midipkt.h"
#include <stdio.h>
#include <string.h>
static int fails, total;
static void run(const char *what, const uint8_t *in, int n, const uint8_t *want, int wn)
{
    MidiPk p; midipk_reset(&p);
    uint8_t out[256]; int k = 0;
    for (int i = 0; i < n; i++) { uint8_t o[4]; if (midipk_feed(&p, in[i], o)) { memcpy(out + k, o, 4); k += 4; } }
    int ok = k == wn && !memcmp(out, want, (size_t)wn);
    total++; if (!ok) fails++;
    printf("%s %s", ok ? "PASS" : "FAIL", what);
    if (!ok) { printf("  got"); for (int i = 0; i < k; i++) printf(" %02X", out[i]); }
    printf("\n");
}
int main(void)
{
    { const uint8_t i[] = { 0x90, 60, 100, 64, 90, 0x80, 60, 0 }, w[] = { 9,0x90,60,100, 9,0x90,64,90, 8,0x80,60,0 };
      run("note on + running status + note off", i, sizeof i, w, sizeof w); }
    { const uint8_t i[] = { 0xC3, 5, 7, 0xD0, 33 }, w[] = { 0xC,0xC3,5,0, 0xC,0xC3,7,0, 0xD,0xD0,33,0 };
      run("program change (1 data) running status, aftertouch", i, sizeof i, w, sizeof w); }
    { const uint8_t i[] = { 0x90, 60, 0xF8, 100 }, w[] = { 0xF,0xF8,0,0, 9,0x90,60,100 };
      run("realtime clock inside a message", i, sizeof i, w, sizeof w); }
    { const uint8_t i[] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 }, w[] = { 4,0xF0,0x7E,0x7F, 7,0x09,0x01,0xF7 };
      run("SysEx GM on (6 bytes)", i, sizeof i, w, sizeof w); }
    { const uint8_t i[] = { 0xF0, 0x41, 0xF7 }, w[] = { 7,0xF0,0x41,0xF7 };
      run("SysEx 3 bytes", i, sizeof i, w, sizeof w); }
    { const uint8_t i[] = { 0xF0, 0x41, 0x10, 0xF7 }, w[] = { 4,0xF0,0x41,0x10, 5,0xF7,0,0 };
      run("SysEx 4 bytes (end alone)", i, sizeof i, w, sizeof w); }
    { const uint8_t i[] = { 0xF2, 1, 2, 0xF3, 4, 0xF6, 0xB0, 7, 100 }, w[] = { 3,0xF2,1,2, 2,0xF3,4,0, 5,0xF6,0,0, 0xB,0xB0,7,100 };
      run("system common: song position, select, tune", i, sizeof i, w, sizeof w); }
    { const uint8_t i[] = { 60, 100, 0xE0, 0, 64 }, w[] = { 0xE,0xE0,0,64 };
      run("data without status dropped, pitch bend", i, sizeof i, w, sizeof w); }
    printf("midipkttest: %d/%d passed\n", total - fails, total);
    return fails != 0;
}
