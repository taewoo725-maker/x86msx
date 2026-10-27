/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* gmtest.c - 3.4.5 SND3-07 소프트웨어 MIDI 음원: FM·타악기·SoundFont 음높이와 끄기 */
#include "../src/gmsynth.h"
#include "../third_party/nukedsc55/sc55w.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t L[48000], R[48000];
static int expect(const char *what, int ok, long got)
{
    printf("%s %-46s %ld\n", ok ? "PASS" : "FAIL", what, got);
    return !ok;
}
static long rms(const int32_t *b, int n) { double s = 0; for (int i = 0; i < n; i++) s += (double)b[i] * b[i]; return (long)__builtin_sqrt(s / n); }
static long freq(const int32_t *b, int n)            /* 오르는 0 지나기 수 → Hz (48 kHz) */
{
    int c = 0;
    for (int i = 1; i < n; i++) if (b[i - 1] < 0 && b[i] >= 0) c++;
    return (long)c * 48000 / n;
}
static void send(const uint8_t *m, int n) { for (int i = 0; i < n; i++) gm_midi(m[i]); }

static const uint8_t *rdbuf; static uint32_t rdlen;
static int rd_cb(uint32_t off, uint8_t *b, uint32_t len) { if (off + len > rdlen) return -1; memcpy(b, rdbuf + off, len); return 0; }
int main(int argc, char **argv)
{
    int f = 0, t = 0;
    gm_set_mode(GM_FM);
    static const uint8_t dry0[] = { 0xB0, 91, 0 };     /* 리버브 보내기 0 (마른 소리로 재기) */
    send(dry0, 3);
    static const uint8_t flute[] = { 0xC0, 73, 0x90, 69, 100 };   /* 플루트 A4 */
    send(flute, 5);
    gm_render(L, R, 4800); gm_render(L, R, 24000);
    f += expect("FM flute A4 ~440 Hz", labs(freq(L, 24000) - 440) <= 6, freq(L, 24000)); t++;
    f += expect("FM loud enough", rms(L, 24000) > 1000, rms(L, 24000)); t++;
    static const uint8_t off[] = { 0x80, 69, 0 };
    send(off, 3);
    gm_render(L, R, 48000); gm_render(L, R, 4800);
    f += expect("FM note off -> silence", rms(L, 4800) < 30, rms(L, 4800)); t++;
    static const uint8_t bend[] = { 0x90, 69, 100, 0xE0, 0x7F, 0x7F };   /* 휨 최대 = +2반음 → 494 Hz */
    send(bend, 6);
    gm_render(L, R, 4800); gm_render(L, R, 24000);
    f += expect("pitch bend +2 semitones ~494 Hz", labs(freq(L, 24000) - 494) <= 8, freq(L, 24000)); t++;
    static const uint8_t pan[] = { 0xB0, 10, 0 };          /* 왼쪽 끝 */
    send(pan, 3); send(off, 3);
    static const uint8_t n2[] = { 0xE0, 0, 64, 0x90, 69, 100 };
    send(n2, 6); gm_render(L, R, 24000); gm_render(L, R, 9600);
    f += expect("pan left: right channel quiet", rms(R, 9600) * 10 < rms(L, 9600), rms(R, 9600)); t++;
    gm_reset();
    static const uint8_t kick[] = { 0x99, 36, 120 };
    send(kick, 3); gm_render(L, R, 4800);
    f += expect("drum kick sounds", rms(L, 4800) > 800, rms(L, 4800)); t++;
    gm_render(L, R, 48000);
    f += expect("drum decays by itself", gm_voices() == 0, gm_voices()); t++;
    static const uint8_t gm_on[] = { 0x90, 60, 100, 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
    send(gm_on, 9);
    f += expect("GM on SysEx resets voices", gm_voices() == 0, gm_voices()); t++;
    /* SoundFont */
    const char *sp = argc > 1 ? argv[1] : "tests/sf2/tiny.sf2";
    FILE *fp = fopen(sp, "rb");
    static uint8_t sf[1 << 20]; size_t sn = fp ? fread(sf, 1, sizeof sf, fp) : 0;
    if (fp) fclose(fp);
    f += expect("SoundFont loads", gm_load_sf2(sf, (uint32_t)sn) == 0 && gm_sf2_presets() == 2, gm_sf2_presets()); t++;
    gm_set_mode(GM_SF2);
    static const uint8_t a5[] = { 0xC0, 0, 0x90, 81, 110 };   /* 음 81 = 기준 69보다 한 옥타브 → 880 Hz */
    send(a5, 5);
    gm_render(L, R, 4800); gm_render(L, R, 24000);
    f += expect("SF2 loop sample key 81 ~880 Hz", labs(freq(L, 24000) - 880) <= 10, freq(L, 24000)); t++;
    f += expect("SF2 loud enough", rms(L, 24000) > 1500, rms(L, 24000)); t++;
    static const uint8_t a5off[] = { 0x80, 81, 0 };
    send(a5off, 3); gm_render(L, R, 48000);
    f += expect("SF2 release -> voice ends", gm_voices() == 0, gm_voices()); t++;
    static const uint8_t dr[] = { 0x99, 38, 100 };
    send(dr, 3); gm_render(L, R, 2400);
    f += expect("SF2 drum bank 128", rms(L, 2400) > 500, rms(L, 2400)); t++;
    gm_render(L, R, 24000);
    f += expect("SF2 one-shot sample ends", gm_voices() == 0, gm_voices()); t++;
    /* 4.0.133: 큰 SoundFont 방식 — 같은 파일을 rd 콜백으로 (목록만 읽고 샘플은 필요할 때) */
    {
        gm_unload_sf2(); gm_reset();
        static uint8_t pool[(1 << 20) + 4096];
        rdbuf = sf; rdlen = (uint32_t)sn;
        f += expect("stream SoundFont loads", gm_load_sf2_stream(rd_cb, (uint32_t)sn, pool, sizeof pool) == 0 && gm_sf2_presets() == 2, gm_sf2_presets()); t++;
        gm_set_mode(GM_SF2);
        send(a5, 5);
        gm_render(L, R, 4800); gm_render(L, R, 24000);
        f += expect("stream loop sample key 81 ~880 Hz", labs(freq(L, 24000) - 880) <= 10, freq(L, 24000)); t++;
        send(a5off, 3); gm_render(L, R, 48000);
        send(dr, 3); gm_render(L, R, 2400);
        f += expect("stream drum bank 128", rms(L, 2400) > 500, rms(L, 2400)); t++;
        gm_render(L, R, 24000);
        f += expect("stream pool too small -> reject", gm_load_sf2_stream(rd_cb, (uint32_t)sn, pool, 100000) == -1, 0); t++;
    }
    /* GS 기계 — SoundFont 칸 둘, 리듬 파트 SysEx, 마스터 음량, 변형 음색 대체 */
    {
        gm_unload_sf2();
        gm_sf_slot(0); f += expect("slot 0 load", gm_load_sf2(sf, (uint32_t)sn) == 0, gm_sf2_presets()); t++;
        gm_sf_slot(1); f += expect("slot 1 empty", gm_sf2_presets() == 0, gm_sf2_presets()); t++;
        f += expect("slot 1 load", gm_load_sf2(sf, (uint32_t)sn) == 0 && gm_sf2_presets() == 2, gm_sf2_presets()); t++;
        gm_unload_sf2();
        gm_sf_slot(0); f += expect("unload slot 1 keeps slot 0", gm_sf2_presets() == 2, gm_sf2_presets()); t++;
        gmi_set_sf(1, 0, 1); gmi_set_mode(1, GM_SF2);
        gmi_set_sf(0, 0, 0); gmi_set_mode(0, GM_SF2);
        static const uint8_t rp[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x11, 0x15, 0x01, 0x19, 0xF7 };   /* 파트 1 = 드럼 */
        for (unsigned i = 0; i < sizeof rp; i++) { gmi_midi(0, rp[i]); gmi_midi(1, rp[i]); }
        gmi_midi(1, 0xB0); gmi_midi(1, 91); gmi_midi(1, 0);   /* 마른 소리로 재기 */
        f += expect("GS rhythm part -> ch1 drum", gmi_drums(1) == 0x201, gmi_drums(1)); t++;
        f += expect("GM instance ignores rhythm part", gmi_drums(0) == 0x200, gmi_drums(0)); t++;
        static const uint8_t d1[] = { 0x90, 38, 100 };
        for (unsigned i = 0; i < sizeof d1; i++) gmi_midi(1, d1[i]);
        gmi_render(1, L, R, 2400);
        long full = rms(L, 2400);
        f += expect("ch1 drum sounds on GS", full > 500, full); t++;
        gmi_render(1, L, R, 24000);
        static const uint8_t mv[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x04, 0x40, 0x3C, 0xF7 };   /* 마스터 음량 64 */
        for (unsigned i = 0; i < sizeof mv; i++) gmi_midi(1, mv[i]);
        f += expect("GS master volume 64", gmi_master(1) == 64, gmi_master(1)); t++;
        for (unsigned i = 0; i < sizeof d1; i++) gmi_midi(1, d1[i]);
        gmi_render(1, L, R, 2400);
        f += expect("master volume halves level", labs(rms(L, 2400) * 2 - full) * 10 < full, rms(L, 2400)); t++;
        gmi_render(1, L, R, 24000);
        static const uint8_t umv[] = { 0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, 0x20, 0xF7 };
        for (unsigned i = 0; i < sizeof umv; i++) gmi_midi(1, umv[i]);
        f += expect("universal master volume 32", gmi_master(1) == 32, gmi_master(1)); t++;
        static const uint8_t gsr[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7 };
        for (unsigned i = 0; i < sizeof gsr; i++) gmi_midi(1, gsr[i]);
        f += expect("GS reset restores drums/master", gmi_drums(1) == 0x200 && gmi_master(1) == 127, gmi_drums(1)); t++;
        static const uint8_t var[] = { 0xB0, 0, 9, 0xC0, 0, 0x90, 81, 110 };   /* 뱅크 9 (없음) → 8 → 0 */
        for (unsigned i = 0; i < sizeof var; i++) gmi_midi(1, var[i]);
        gmi_render(1, L, R, 4800);
        f += expect("GS variation falls back to capital", rms(L, 4800) > 1000, rms(L, 4800)); t++;
        gmi_set_sf(1, 0, 0);
    }
    /* 효과·GS 파트 설정 (FM 음원으로) */
    {
        gm_use(2); gm_set_mode(GM_FM); gm_reset();
        static const uint8_t n1[] = { 0xC0, 73, 0x90, 69, 100 }, o1[] = { 0x80, 69, 0 };
        send(n1, 5); gm_render(L, R, 9600); send(o1, 3); gm_render(L, R, 9600); gm_render(L, R, 9600);
        long tail = rms(L, 9600);
        f += expect("reverb tail after note off (send 40)", tail > 30, tail); t++;
        gm_reset();
        static const uint8_t r0[] = { 0xB0, 91, 0 };
        send(r0, 3); send(n1, 5); gm_render(L, R, 9600); send(o1, 3); gm_render(L, R, 9600); gm_render(L, R, 9600);
        f += expect("no tail with send 0", rms(L, 9600) < 30, rms(L, 9600)); t++;
        gm_reset(); send(r0, 3);
        static const uint8_t rl0[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x01, 0x33, 0x00, 0x0C, 0xF7 };   /* 리버브 레벨 0 */
        send(rl0, sizeof rl0);
        static const uint8_t c127[] = { 0xB0, 93, 127 };
        send(n1, 5); gm_render(L, R, 9600); long dry = rms(L, 9600);
        send(c127, 3); gm_render(L, R, 9600); long wet = rms(L, 9600);
        f += expect("chorus send changes sound (live CC93)", labs(wet - dry) * 20 > dry, wet); t++;
        send(o1, 3); gm_render(L, R, 48000); gm_reset(); send(r0, 3);
        send(n1, 5); gm_render(L, R, 4800); gm_render(L, R, 9600); long a = rms(L, 9600);
        static const uint8_t v64[] = { 0xB0, 7, 64 };
        send(v64, 3); gm_render(L, R, 9600); long b = rms(L, 9600);
        f += expect("CC7 acts on sounding note", b * 3 < a * 2, b); t++;
        send(o1, 3); gm_render(L, R, 48000); gm_reset(); send(r0, 3);
        static const uint8_t rpn[] = { 0xB0, 101, 0, 0xB0, 100, 2, 0xB0, 6, 76 };   /* 거친 조율 +12 */
        send(rpn, 9); send(n1, 5); gm_render(L, R, 4800); gm_render(L, R, 24000);
        f += expect("RPN coarse tune +12 -> ~880 Hz", labs(freq(L, 24000) - 880) <= 12, freq(L, 24000)); t++;
        send(o1, 3); gm_render(L, R, 48000); gm_reset(); send(r0, 3);
        static const uint8_t dl[] = { 0xB9, 99, 0x1A, 0xB9, 98, 36, 0xB9, 6, 0 }, k1[] = { 0x99, 36, 120 };   /* NRPN 드럼 36 레벨 0 */
        send(k1, 3); gm_render(L, R, 4800); long kk = rms(L, 4800); gm_render(L, R, 48000);
        send(dl, 9); send(k1, 3); gm_render(L, R, 4800);
        f += expect("GS drum NRPN level 0 silences kick", rms(L, 4800) * 20 < kk, rms(L, 4800)); t++;
        gm_reset(); send(r0, 3);
        static const uint8_t ks[] = { 0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x11, 0x16, 0x4C, 0x0D, 0xF7 };   /* 파트 1 키 시프트 +12 (GS 기계만) */
        gm_use(2); gmi_set_sf(2, 0, 1); gm_set_mode(GM_FM); send(r0, 3); send(ks, sizeof ks);
        send(n1, 5); gm_render(L, R, 4800); gm_render(L, R, 24000);
        f += expect("GS part key shift +12 -> ~880 Hz", labs(freq(L, 24000) - 880) <= 12, freq(L, 24000)); t++;
        gmi_set_sf(2, 0, 0); gm_use(0);
    }
    /* SoundFont 거르개 (NRPN 차단·공명) — 소리가 나고 터지지 않음 */
    {
        gm_use(3); gm_sf_slot(0); gmi_set_sf(3, 0, 1); gm_use(3); gm_set_mode(GM_SF2);
        static const uint8_t r0[] = { 0xB0, 91, 0 }, a5n[] = { 0xC0, 0, 0x90, 81, 110 }, a5f[] = { 0x80, 81, 0 };
        send(r0, 3); send(a5n, 5); gm_render(L, R, 4800); gm_render(L, R, 9600); long un = rms(L, 9600);
        send(a5f, 3); gm_render(L, R, 48000);
        static const uint8_t cut[] = { 0xB0, 99, 1, 0xB0, 98, 0x20, 0xB0, 6, 0, 0xB0, 98, 0x21, 0xB0, 6, 127 };
        send(cut, sizeof cut); send(a5n, 5); gm_render(L, R, 4800); gm_render(L, R, 9600); long fl = rms(L, 9600);
        f += expect("SF2 filter (cutoff 0, reso 127) sounds, stable", fl > un / 4 && fl < un * 4, fl); t++;
        f += expect("SF2 filtered pitch kept ~880 Hz", labs(freq(L, 9600) - 880) <= 15, freq(L, 9600)); t++;
        gmi_set_sf(3, 0, 0); gm_use(0);
    }
    /* SC-55 실기 롬 (Nuked-SC55) — 롬 없으면 SoundFont/FM 으로, 롬을 넣으면 그 모드 (여기서는 가짜 롬: 크기 검사·돌아감만) */
    {
        gm_use(2); gmi_set_sf(2, 0, 1); gm_use(2); gm_set_mode(GM_SC55);
        f += expect("SC55 without ROM falls back", gm_mode() != GM_SC55, gm_mode()); t++;
        f += expect("SC55 wrong size rejected", sc55w_load(SC55_MK2, 0, 100) == -1, 0); t++;
        static const uint32_t sz[5] = { 0x8000, 0x80000, 0x200000, 0x100000, 0x1000 };
        int ok = 1; for (int i = 0; i < 5; i++) { memset(sc55w_tmp(), 0xFF, sz[i]); ok &= sc55w_load(SC55_MK2, i, sz[i]) == 0; }
        f += expect("SC55 fake ROM set loads", ok && sc55w_start(SC55_MK2) == 0, ok); t++;
        gm_set_mode(GM_SC55);
        f += expect("SC55 mode on", gm_mode() == GM_SC55, gm_mode()); t++;
        gm_use(3); gm_set_mode(GM_SC55);
        f += expect("second SC55 instance refused (one engine)", gm_mode() != GM_SC55, gm_mode()); t++;
        gm_set_mode(GM_FM); gm_use(2);
        gm_render(L, R, 4800);
        f += expect("SC55 renders (MCU steps run)", sc55w_steps() > 100000, (long)sc55w_steps()); t++;
        static char tx[17]; sc55w_lcd_text(tx);
        f += expect("SC55 LCD text 16 chars", strlen(tx) == 16, (long)strlen(tx)); t++;
        gm_set_mode(GM_FM); sc55w_close(); gmi_set_sf(2, 0, 0); gm_use(0);
    }
    /* 음량 맞춤 (표준 구절 RMS → 4000) 과 목소리 뺏길 때 틱 */
    {
        gm_use(0); gm_set_mode(GM_FM); gm_reset();
        for (int fr = 0; fr < 12; fr++) gm_render(L, R, 800);                 /* 한 프레임에 한 박씩 재서 8프레임 안에 끝 */
        int tr = gmi_trim(0);
        f += expect("calibrated FM trim in range", tr >= 51 && tr <= 1024 && tr != 192 + 1000, tr); t++;
        static const uint8_t ph[] = { 0xB0, 91, 0, 0xB1, 91, 0, 0xB2, 91, 0, 0xC0, 0, 0xC1, 48, 0xC2, 33 };
        send(ph, sizeof ph);
        double sum = 0;
        for (int beat = 0; beat < 8; beat++) {
            static const uint8_t on[] = { 0x90, 60, 100, 0x90, 64, 100, 0x90, 67, 100, 0x91, 55, 90, 0x91, 59, 90, 0x91, 62, 90, 0x92, 36, 100, 0x99, 36, 110, 0x99, 42, 80 };
            static const uint8_t of[] = { 0x80, 60, 0, 0x80, 64, 0, 0x80, 67, 0, 0x81, 55, 0, 0x81, 59, 0, 0x81, 62, 0, 0x82, 36, 0 };
            send(on, sizeof on); gm_render(L, R, 6000); double r = (double)rms(L, 6000); sum += r * r; send(of, sizeof of);
        }
        long lv = (long)__builtin_sqrt(sum / 8);
        f += expect("standard phrase level near target 4000", lv > 3000 && lv < 5000, lv); t++;
        gm_reset();
        /* 목소리 뺏기: 가장 오래된 큰 낮은 음(41 Hz)이 뺏길 때 한 표본 사이 튐 — 유령(4 ms 줄이기) 없이 바로 끊으면 그 음의 순간 크기만큼 튐 */
        gm_reset(); gm_midi(0xB0); gm_midi(91); gm_midi(0); gm_midi(0xC0); gm_midi(19);
        gm_midi(0x90); gm_midi(28); gm_midi(127);                                            /* 가장 오래된 큰 음 */
        for (int k = 0; k < 47; k++) { gm_midi(0x90); gm_midi((uint8_t)(50 + k)); gm_midi(2); }
        for (int fr = 0; fr < 40; fr++) gm_render(L, R, 800);
        long s0 = 0, s1 = 0; int32_t prev = L[799];
        for (int fr = 0; fr < 5; fr++) { gm_render(L, R, 800); for (int i = 0; i < 800; i++) { long d = L[i] - prev; prev = L[i]; if (d < 0) d = -d; if (d > s0) s0 = d; } }
        gm_midi(0x90); gm_midi(110); gm_midi(2);                                             /* 꽉 찬 상태에서 새 음 = 가장 오래된 음을 뺏음 */
        for (int fr = 0; fr < 2; fr++) { gm_render(L, R, 800); for (int i = 0; i < 800; i++) { long d = L[i] - prev; prev = L[i]; if (d < 0) d = -d; if (d > s1) s1 = d; } }
        f += expect("voice steal: loud note cut without step", s1 <= s0 * 2 + 100, s1 * 100 / (s0 ? s0 : 1)); t++;
        gm_reset();
    }
    printf("gmtest: %d/%d passed\n", t - f, t);
    return f != 0;
}
