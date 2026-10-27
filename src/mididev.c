/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mididev.c - MIDI 기계 부품 틀 (5.2.50). 설명은 mididev.h */
#include "mididev.h"
#include "gmsynth.h"
#include <string.h>

const MidiDevClass midi_dev_class[MDK_N] = {
    [MDK_NONE] = { "NONE", "-", "-", 0, 0 },
    [MDK_MT32] = { "MT32", "Roland MT-32 / CM-32L", "MT-32", 1, 2 },
    [MDK_GM]   = { "GM",   "GM Sound Module", "GM", 1, 2 },
    [MDK_GS]   = { "SC55", "Roland SC-55 (ROM / GS SoundFont)", "SC-55", 1, 2 },   /* 5.2.57 (5.2.60: opt 0 실기 롬 Nuked-SC55, 1 GS SoundFont) */
    [MDK_USB]  = { "USB",  "USB MIDI Device", "USB", 0, 0 },
    [MDK_FILE] = { "FILE", "MIDI File Recorder (.mid)", "REC", 0, 0 },
};

MidiBus midi_bus;

static void soft_pick(void)                                 /* 소프트웨어 기계는 모두 active — 기계 i 는 음원 인스턴스 i (gmsynth GM_INST_MAX = MIDI_DEV_MAX) */
{
    for (int i = 0; i < midi_bus.n; i++) {
        MidiDev *d = &midi_bus.dev[i];
        d->active = (uint8_t)(midi_dev_class[d->kind].soft != 0);
    }
}

void midibus_clear(void) { memset(&midi_bus, 0, sizeof midi_bus); }

int midibus_plug(int kind, int opt)
{
    if (kind <= MDK_NONE || kind >= MDK_N || midi_bus.n >= MIDI_DEV_MAX) return -1;
    MidiDev *d = &midi_bus.dev[midi_bus.n];
    memset(d, 0, sizeof *d);
    d->kind = (uint8_t)kind; d->opt = (uint8_t)opt; d->vol = 100; d->rx_mask = 0xFFFF; d->port = MDP_ANY;
    midi_bus.n++;
    soft_pick();                                            /* n 을 먼저 늘림 (전에는 방금 꽂은 기계가 다음 꽂기 전까지 active 0 = 소리 없음) */
    return midi_bus.n - 1;
}

void midibus_unplug(int i)
{
    if (i < 0 || i >= midi_bus.n) return;
    for (int k = i; k + 1 < midi_bus.n; k++) midi_bus.dev[k] = midi_bus.dev[k + 1];
    midi_bus.n--;
    for (int k = i; k < MIDI_DEV_MAX; k++) gmi_set_mode(k, GM_OFF);   /* 자리가 밀린 음원 인스턴스는 끔 (프런트엔드가 다시 켬) */
    memset(&midi_bus.dev[midi_bus.n], 0, sizeof(MidiDev));
    soft_pick();
}

MidiDev *midibus_find(int kind) { for (int i = 0; i < midi_bus.n; i++) if (midi_bus.dev[i].kind == kind) return &midi_bus.dev[i]; return 0; }
int midibus_soft(void) { for (int i = 0; i < midi_bus.n; i++) if (midi_bus.dev[i].active) return i; return -1; }
int midibus_dev_soft_on(void) { for (int i = 0; i < midi_bus.n; i++) if (midi_bus.dev[i].active && gmi_mode(i) != GM_OFF) return 1; return 0; }   /* 기본 음원 빼고 — 믹서 MIDI 채널 표시용 */
int midibus_any_soft_on(void) { if (midibus_def()) return 1; for (int i = 0; i < midi_bus.n; i++) if (midi_bus.dev[i].active && gmi_mode(i) != GM_OFF) return 1; return 0; }
int midibus_def(void) { return midi_bus.def_on && gmi_mode(GM_INST_GT) != GM_OFF; }   /* 기계가 꽂혀 있어도 GT 내장은 따로 울림 */   /* 5.2.70 */   /* 5.2.51 */

int midibus_gm_mode(const MidiDev *d)
{
    if (!d || !midi_dev_class[d->kind].soft) return GM_OFF;
    if (d->kind == MDK_MT32) return GM_MT32;
    if (d->kind == MDK_GS) return d->opt ? GM_SF2 : GM_SC55; /* 실기 롬 (없으면 프런트엔드가 GS SoundFont → FM) */
    return d->opt ? GM_FM : GM_SF2;
}

/* 기계 화면에 쓰는 SysEx (sx 는 F0 다음부터 F7 앞까지)
 *  MT-32/CM-32L 표시 글자: 41 dd 16 12 20 00 00 <글자 20까지> cs
 *  SC-55 계열 글자:         41 dd 45 12 10 00 00 <글자 32까지> cs
 *  SC-55 계열 점 그림:      41 dd 45 12 10 01 00 <64 바이트> cs — 16 줄 × 5 칸 묶음 넷 (0~15 = 열 0~4, 16~31 = 5~9, 32~47 = 10~14, 48~63 = 15), 비트 4 = 왼쪽 */
void midibus_sysex_done(MidiDev *d)
{
    const uint8_t *x = d->sx; int n = d->sxn;
    if (n < 9 || x[0] != 0x41 || x[3] != 0x12) return;
    int dlen = n - 8;                                       /* 주소 뒤 자료 (끝 cs 뺌) */
    const uint8_t *dat = x + 7;
    if (d->kind == MDK_MT32 && x[2] == 0x16 && x[4] == 0x20 && x[5] == 0x00 && x[6] == 0x00) {
        int k = 0; for (; k < dlen && k < 20; k++) d->lcd[k] = (char)(dat[k] >= 0x20 && dat[k] < 0x7F ? dat[k] : ' ');
        d->lcd[k] = 0; d->lcd_seq++;
    } else if (d->kind == MDK_GS && x[2] == 0x45 && x[4] == 0x10 && x[6] == 0x00) {
        if (x[5] == 0x00) {
            int k = 0; for (; k < dlen && k < 32; k++) d->lcd[k] = (char)(dat[k] >= 0x20 && dat[k] < 0x7F ? dat[k] : ' ');
            d->lcd[k] = 0; d->lcd_seq++;
        } else if (x[5] == 0x01 && dlen >= 64) {
            for (int r = 0; r < 16; r++) {
                uint16_t v = 0;
                for (int g = 0; g < 4; g++) { uint8_t b = dat[g * 16 + r]; int w = g < 3 ? 5 : 1; for (int c = 0; c < w; c++) if (b & (0x10 >> c)) v |= (uint16_t)(0x8000 >> (g * 5 + c)); }
                d->dot[r] = v;
            }
            d->dot_seq++;
        }
    }
}

/* 바이트 하나: 채널 메시지(8x~Ex) 는 그 채널을 받는 기계에만, 시스템(F0~FF)·SysEx 안 바이트는 모두에게 */
void midibus_feed(uint8_t b, uint32_t t, int src)
{
    MidiBus *bus = &midi_bus;
    if (bus->def_on && src == MDP_MSXMIDI) gmi_midi(GM_INST_GT, b);   /* GT 내장 기본 음원 (5.2.74: 기계가 있어도 같이 — GTMIDI 띠) */
    if (!bus->n) return;
    int ch = -1, idx = -1;                                  /* -1 = 모두 / 자료 바이트 차례 */
    if (b >= 0xF8) { }                                      /* 실시간: 상태 안 바뀜, 모두 */
    else if (b == 0xF0) { bus->sysex = 1; bus->run = 0; }
    else if (b == 0xF7) bus->sysex = 0;
    else if (b >= 0xF1) bus->run = 0;                       /* 시스템 공통 */
    else if (b & 0x80) { bus->run = b; ch = b & 0x0F; bus->dat1 = 0; }
    else if (bus->sysex) { }
    else if (bus->run) { ch = bus->run & 0x0F; idx = bus->dat1; bus->dat1 = (int8_t)(((bus->run & 0xF0) == 0xC0 || (bus->run & 0xF0) == 0xD0) ? 0 : !idx); }   /* 자료 바이트 차례 0·1 (실행 상태로 되풀이) */
    for (int i = 0; i < bus->n; i++) {
        MidiDev *d = &bus->dev[i];
        if (d->port != MDP_ANY && d->port != src) continue;    /* 다른 단자에 꽂힌 기계 */
        if (ch >= 0 && !(d->rx_mask & (1u << ch))) continue;
        if (ch >= 0 && !(b & 0x80) && d->run != bus->run) { d->run = bus->run; /* 이 기계는 상태를 못 봤음 → 상태 먼저 */
            if (d->active) gmi_midi(i, bus->run);
            else if (!midi_dev_class[d->kind].soft && d->qn < MIDI_DEV_Q) { d->q[d->qn] = bus->run; d->qt[d->qn++] = t; }
        }
        if (ch >= 0 && (b & 0x80)) d->run = b;
        if (idx == 1 && (bus->run & 0xF0) == 0x90 && b) d->ch_on[ch]++;   /* Note On 세기 > 0 (MIDI 믹서 창의 채널 불) */
        d->bytes++;
        if (d->active) {                                    /* 소프트웨어 기계의 화면 SysEx */
            if (b == 0xF0) { d->sxn = 0; d->sx_over = 0; }
            else if (b == 0xF7) { if (!d->sx_over) midibus_sysex_done(d); d->sxn = 0; }
            else if (bus->sysex && !(b & 0x80)) { if (d->sxn < sizeof d->sx) d->sx[d->sxn++] = b; else d->sx_over = 1; }
            gmi_midi(i, b);
        }
        else if (!midi_dev_class[d->kind].soft && d->qn < MIDI_DEV_Q) { d->q[d->qn] = b; d->qt[d->qn++] = t; }
    }
}

int midibus_take(int i, uint8_t *out, uint32_t *out_t, int max)
{
    if (i < 0 || i >= midi_bus.n) return 0;
    MidiDev *d = &midi_bus.dev[i];
    int n = d->qn < max ? d->qn : max;
    memcpy(out, d->q, (size_t)n);
    if (out_t) memcpy(out_t, d->qt, (size_t)n * sizeof(uint32_t));
    d->qn = 0;
    return n;
}
