/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* midibustest.c - 5.2.50: MIDI 기계 부품 틀 — 꽂기·빼기, 채널 나눠 보내기(받는 채널 비트·실행 상태), SysEx 는 모두, 바깥 기계 큐, 소프트웨어 기계는 하나만 */
#include "../src/mididev.h"
#include "../src/gmsynth.h"
#include <stdio.h>
#include <string.h>

static int f, t;
static void expect(const char *what, int got, int want) { printf("%s %-50s %d (want %d)\n", got == want ? "PASS" : "FAIL", what, got, want); t++; if (got != want) f++; }
static uint8_t ob[MIDI_DEV_Q]; static uint32_t ot[MIDI_DEV_Q];

int main(void)
{
    midibus_clear();
    expect("empty", midi_bus.n, 0);
    expect("bad kind", midibus_plug(MDK_NONE, 0), -1);
    int u = midibus_plug(MDK_USB, 0), g = midibus_plug(MDK_GM, 1), m = midibus_plug(MDK_MT32, 1), r = midibus_plug(MDK_FILE, 0);
    expect("4 plugged", midi_bus.n, 4); expect("5th refused", midibus_plug(MDK_USB, 0), -1);
    expect("default rx all", midi_bus.dev[u].rx_mask, 0xFFFF); expect("default vol 100", midi_bus.dev[u].vol, 100);
    expect("soft = first soft device (GM)", midibus_soft(), g); expect("MT-32 also active (5.2.51 instances)", midi_bus.dev[m].active, 1);
    expect("gm mode of GM/FM", midibus_gm_mode(&midi_bus.dev[g]), GM_FM); expect("gm mode of MT32", midibus_gm_mode(&midi_bus.dev[m]), GM_MT32);
    expect("gm mode of USB", midibus_gm_mode(&midi_bus.dev[u]), GM_OFF);
    expect("find FILE", midibus_find(MDK_FILE) == &midi_bus.dev[r], 1);
    /* 채널 나누기: USB 는 채널 1 만, 파일은 채널 2 만 */
    midi_bus.dev[u].rx_mask = 1 << 0; midi_bus.dev[r].rx_mask = 1 << 1;
    midibus_feed(0x90, 10, MDP_MSXMIDI); midibus_feed(60, 11, MDP_MSXMIDI); midibus_feed(100, 12, MDP_MSXMIDI);     /* 채널 1 Note On */
    midibus_feed(0x91, 20, MDP_MSXMIDI); midibus_feed(62, 21, MDP_MSXMIDI); midibus_feed(100, 22, MDP_MSXMIDI);     /* 채널 2 */
    midibus_feed(64, 30, MDP_MSXMIDI); midibus_feed(100, 31, MDP_MSXMIDI);                             /* 실행 상태 (채널 2) */
    int n = midibus_take(u, ob, ot, MIDI_DEV_Q);
    expect("USB got only channel 1 (3 bytes)", n, 3); expect("USB byte 0", ob[0], 0x90); expect("USB time", (int)ot[2], 12);
    n = midibus_take(r, ob, ot, MIDI_DEV_Q);
    expect("FILE got channel 2 (3 + 2 running)", n, 5); expect("FILE byte 0", ob[0], 0x91); expect("FILE running data", ob[3], 64);
    expect("queue emptied", midibus_take(r, ob, ot, MIDI_DEV_Q), 0);
    /* 상태를 못 본 기계가 실행 상태 자료를 받을 때: 상태 먼저 */
    midi_bus.dev[u].rx_mask = 0xFFFF;                                        /* 이제 USB 도 채널 2 받음 (상태 0x91 은 못 봤음) */
    midibus_feed(65, 40, MDP_MSXMIDI); midibus_feed(100, 41, MDP_MSXMIDI);
    n = midibus_take(u, ob, ot, MIDI_DEV_Q);
    expect("status re-sent before data", n, 3); expect("re-sent status 91", ob[0], 0x91); expect("then data", ob[1], 65);
    /* SysEx·실시간은 모두에게 */
    midi_bus.dev[u].rx_mask = 1 << 5;
    midibus_feed(0xF0, 50, MDP_MSXMIDI); midibus_feed(0x41, 51, MDP_MSXMIDI); midibus_feed(0xF7, 52, MDP_MSXMIDI); midibus_feed(0xF8, 53, MDP_MSXMIDI);
    expect("USB got sysex + clock", midibus_take(u, ob, ot, MIDI_DEV_Q), 4);
    expect("FILE got ch2 running data (2) + sysex + clock", midibus_take(r, ob, ot, MIDI_DEV_Q), 6);
    expect("bytes counted", (int)midi_bus.dev[r].bytes, 5 + 2 + 4);
    /* 여러 소프트웨어 기계 동시: 인스턴스 g·m 에 각각 FM 음을 넣으면 따로 울림 */
    gmi_set_mode(g, GM_FM); gmi_set_mode(m, GM_FM);
    midi_bus.dev[g].rx_mask = 1 << 0; midi_bus.dev[m].rx_mask = 1 << 1;
    midibus_feed(0x90, 0, MDP_MSXMIDI); midibus_feed(60, 0, MDP_MSXMIDI); midibus_feed(100, 0, MDP_MSXMIDI);          /* 채널 1 → GM 기계만 */
    expect("inst g has a voice", gmi_voices(g), 1); expect("inst m silent", gmi_voices(m), 0);
    midibus_feed(0x91, 0, MDP_MSXMIDI); midibus_feed(64, 0, MDP_MSXMIDI); midibus_feed(100, 0, MDP_MSXMIDI);          /* 채널 2 → MT-32 자리(FM 대신) */
    expect("inst m has a voice", gmi_voices(m), 1); expect("inst g still 1", gmi_voices(g), 1);
    expect("any soft on", midibus_any_soft_on(), 1);
    /* 빼기: 앞의 GM 을 빼면 MT-32 가 첫 소프트웨어 기계, 밀린 인스턴스는 꺼짐 */
    midibus_unplug(g);
    expect("3 left", midi_bus.n, 3); expect("MT-32 now first soft", midi_bus.dev[midibus_soft()].kind, MDK_MT32);
    expect("shifted instances off", gmi_mode(g), GM_OFF);
    expect("USB still first", midi_bus.dev[0].kind, MDK_USB);
    /* 단자: USB 를 SFG 단자에만 꽂으면 MSX-MIDI 가 보낸 것은 안 받음 */
    midi_bus.dev[0].port = MDP_SFG; midi_bus.dev[0].rx_mask = 0xFFFF;
    midibus_feed(0xF8, 0, MDP_MSXMIDI); expect("port SFG ignores MSX-MIDI bytes", midibus_take(0, ob, ot, MIDI_DEV_Q), 0);
    midibus_feed(0xF8, 0, MDP_SFG); expect("port SFG gets SFG bytes", midibus_take(0, ob, ot, MIDI_DEV_Q), 1);
    midi_bus.dev[0].port = MDP_ANY; midibus_feed(0xF8, 0, MDP_MSXMIDI); expect("port ANY gets all", midibus_take(0, ob, ot, MIDI_DEV_Q), 1);
    /* 화면 SysEx — MT-32 표시 글자, SC-55 글자·점 그림 (다른 종류 기계는 무시) */
    {
        midibus_clear();
        int a = midibus_plug(MDK_MT32, 0), b = midibus_plug(MDK_GS, 0);
        expect("last plugged soft device is active", midi_bus.dev[b].active, 1);   /* 5.2.58 고친 것 */
        static const uint8_t mt[] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x20, 0x00, 0x00, 'S','i','e','r','r','a',' ','O','n','-','L','i','n','e', 0x00, 0xF7 };
        for (unsigned i = 0; i < sizeof mt; i++) midibus_feed(mt[i], 0, MDP_MSXMIDI);
        expect("MT-32 lcd text", !strcmp(midi_bus.dev[a].lcd, "Sierra On-Line"), 1);
        expect("MT-32 lcd seq", midi_bus.dev[a].lcd_seq, 1);
        expect("GS ignores MT-32 text", midi_bus.dev[b].lcd_seq, 0);
        static const uint8_t gs[] = { 0xF0, 0x41, 0x10, 0x45, 0x12, 0x10, 0x00, 0x00, 'H','e','l','l','o',' ','G','S', 0x00, 0xF7 };
        for (unsigned i = 0; i < sizeof gs; i++) midibus_feed(gs[i], 0, MDP_MSXMIDI);
        expect("SC-55 lcd text", !strcmp(midi_bus.dev[b].lcd, "Hello GS"), 1);
        expect("MT-32 ignores SC-55 text", midi_bus.dev[a].lcd_seq, 1);
        uint8_t dm[8 + 64 + 2] = { 0xF0, 0x41, 0x10, 0x45, 0x12, 0x10, 0x01, 0x00 };
        dm[8 + 0] = 0x10;          /* 줄 0 열 0 */
        dm[8 + 16 + 3] = 0x01;     /* 줄 3 열 9 */
        dm[8 + 48 + 15] = 0x10;    /* 줄 15 열 15 */
        dm[8 + 64] = 0; dm[8 + 65] = 0xF7;
        for (unsigned i = 0; i < sizeof dm; i++) midibus_feed(dm[i], 0, MDP_MSXMIDI);
        expect("dot seq", midi_bus.dev[b].dot_seq, 1);
        expect("dot r0 c0", midi_bus.dev[b].dot[0], 0x8000);
        expect("dot r3 c9", midi_bus.dev[b].dot[3], 0x8000 >> 9);
        expect("dot r15 c15", midi_bus.dev[b].dot[15], 1);
    }
    midibus_clear(); expect("cleared", midibus_soft(), -1); expect("no soft on", midibus_any_soft_on(), 0);
    /* GT 내장 기본 음원 (5.2.74: 인스턴스 GM_INST_GT, 기계와 같이 울림) */
    midibus_clear(); gmi_set_mode(GM_INST_GT, GM_FM);
    midibus_feed(0x90, 0, MDP_MSXMIDI); midibus_feed(60, 0, MDP_MSXMIDI); midibus_feed(100, 0, MDP_MSXMIDI);
    expect("no device, default off -> silent", gmi_voices(GM_INST_GT), 0);
    midi_bus.def_on = 1;
    expect("default synth counts as soft on", midibus_any_soft_on(), 1);
    expect("default synth is not a device", midibus_dev_soft_on(), 0);
    midibus_feed(0x90, 0, MDP_MSXMIDI); midibus_feed(62, 0, MDP_MSXMIDI); midibus_feed(100, 0, MDP_MSXMIDI);
    expect("no device + GT internal -> default synth sounds", gmi_voices(GM_INST_GT) > 0, 1);
    int gi2 = midibus_plug(MDK_GM, 1); gmi_set_mode(gi2, GM_FM);
    expect("device plugged -> GT internal still on", midibus_def(), 1);
    int v0 = gmi_voices(GM_INST_GT);
    midibus_feed(0x90, 0, MDP_MSXMIDI); midibus_feed(64, 0, MDP_MSXMIDI); midibus_feed(100, 0, MDP_MSXMIDI);
    expect("both sound: GT internal", gmi_voices(GM_INST_GT) > v0, 1); expect("both sound: module", gmi_voices(gi2) > 0, 1);
    midibus_clear(); gmi_set_mode(GM_INST_GT, GM_OFF); gmi_set_mode(gi2, GM_OFF); midi_bus.def_on = 0;
    printf("midibustest: %d/%d passed\n", t - f, t);
    return f != 0;
}
