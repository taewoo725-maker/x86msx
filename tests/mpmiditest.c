/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mpmiditest.c - 8.0.25 다중CPU: MIDI 음원(GM FM) 합성을 다른 스레드(일꾼 코어 흉내)가 할 때 나오는 소리가
 * 메인 혼자 할 때와 샘플 하나까지 같은지 본다. Z80 프로그램이 MSX-MIDI(E8h)로 음을 계속 보내고, 프레임마다 out_l·out_r 를 해시.
 *   build/mpmiditest [machines/DPC-200] [par]   → "hash XXXXXXXX energy N frames N midi N" 한 줄 */
#include "../host/msx_host.h"
#include "../src/mididev.h"
#include "../src/gmsynth.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static MSX m;
typedef struct { void (*fn)(void *); void *a; } Job;
static pthread_t jt; static Job jb;
static void *thr(void *x) { Job *j = (Job *)x; j->fn(j->a); return 0; }
static int bug;
static void nop(void *a) { (void)a; }
static int post(void (*fn)(void *), void *arg) { static int c; if (bug && ++c == 400) fn = nop; jb.fn = fn; jb.a = arg; pthread_create(&jt, 0, thr, &jb); return 0; }
static int waitj(int h) { (void)h; pthread_join(jt, 0); return 1; }
extern int (*msx_job_post)(void (*)(void *), void *);
extern int (*msx_job_wait)(int);
extern uint32_t msx_mp_midi_n;

int main(int argc, char **argv)
{
    char err[256];
    int par = argc > 2 && !strcmp(argv[2], "par"); bug = argc > 3;
    if (msx_host_init(&m, argc > 1 ? argv[1] : "machines/DPC-200", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    m.prof.midi = 1;
    msx_io_rebuild(&m);
    msx_reset(&m);
    midibus_clear();
    int g = midibus_plug(MDK_GM, 1);
    gmi_set_mode(g, GM_FM);
    if (par) { msx_job_post = post; msx_job_wait = waitj; }
    for (int i = 0; i < 60; i++) msx_run_frame(&m);
    /* 음 보내는 Z80 프로그램 (C000h): TxRDY 를 보고 표의 바이트를 E8h 로, 잠깐 쉬고 되풀이 */
    static const uint8_t data[] = { 0xC0, 0x00, 0x90, 0x3C, 0x64, 0x90, 0x40, 0x64, 0x90, 0x43, 0x50, 0xC1, 0x30, 0x91, 0x30, 0x70, 0xB1, 0x0A, 0x20,
                                    0x80, 0x3C, 0x00, 0x80, 0x40, 0x00, 0x90, 0x48, 0x60, 0x91, 0x37, 0x7F, 0x80, 0x43, 0x00 };
    uint8_t prog[64]; int k = 0;
    uint16_t dat = 0xC040;
    prog[k++] = 0xF3;                                          /* DI */
    int start = k;
    prog[k++] = 0x21; prog[k++] = dat & 0xFF; prog[k++] = dat >> 8;   /* LD HL,data */
    prog[k++] = 0x06; prog[k++] = sizeof data;                 /* LD B,n */
    int loop = k;
    prog[k++] = 0xDB; prog[k++] = 0xE9;                        /* IN A,(E9h) */
    prog[k++] = 0xE6; prog[k++] = 0x01;                        /* AND 1 (TxRDY) */
    prog[k++] = 0x28; prog[k] = (uint8_t)(loop - (k + 1)); k++;   /* JR Z,loop */
    prog[k++] = 0x7E; prog[k++] = 0xD3; prog[k++] = 0xE8; prog[k++] = 0x23;   /* LD A,(HL) / OUT (E8h),A / INC HL */
    prog[k++] = 0x10; prog[k] = (uint8_t)(loop - (k + 1)); k++;   /* DJNZ loop */
    prog[k++] = 0x11; prog[k++] = 0x00; prog[k++] = 0x60;     /* LD DE,6000h (쉼) */
    int dl = k;
    prog[k++] = 0x1B; prog[k++] = 0x7A; prog[k++] = 0xB3;     /* DEC DE / LD A,D / OR E */
    prog[k++] = 0x20; prog[k] = (uint8_t)(dl - (k + 1)); k++;  /* JR NZ,dl */
    prog[k++] = 0x18; prog[k] = (uint8_t)(start - (k + 1)); k++;   /* JR start */
    for (int i = 0; i < k; i++) msx_mem_write(&m, (uint16_t)(0xC000 + i), prog[i]);
    for (int i = 0; i < (int)sizeof data; i++) msx_mem_write(&m, (uint16_t)(dat + i), data[i]);
    msx_io_write(&m, 0xE9, 0x4E); msx_io_write(&m, 0xE9, 0x05);   /* 8251 모드, TxEN */
    m.cpu.pc = 0xC000;
    uint32_t h = 2166136261u; uint64_t energy = 0; int midi = 0;
    for (int f = 0; f < 600; f++) {
        msx_run_frame(&m);
        midi += m.midi_n;
        for (int i = 0; i < m.psg.buf_n; i++) {
            int16_t l = m.out_l[i], r = m.out_r[i];
            h = (h ^ (uint16_t)l) * 16777619u; h = (h ^ (uint16_t)r) * 16777619u;
            energy += (uint64_t)((int64_t)l * l) >> 8;
        }
        if (f == 300) gmi_set_mode(g, GM_FM);           /* 중간에 바꾸기 (메인에서 음원을 만지는 경우 — 일 바깥) */
    }
    printf("hash %08X energy %llu frames 600 midi %d par_frames %u\n", h, (unsigned long long)energy, midi, msx_mp_midi_n);
    return 0;
}
