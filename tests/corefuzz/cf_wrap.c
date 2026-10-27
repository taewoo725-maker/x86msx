/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* corefuzz 감싸개: 코어 하나(CF_HDR 의 Z80)를 CpuState 로 감쌈. -DCF_PFX=cur_ 또는 ref_ , -DCF_HDR='"z80.h"' */
#include CF_HDR
#include "cf.h"
#include <string.h>
#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
#define N(x) CAT(CF_PFX, x)

static Z80 z;
static uint8_t mem[65536];
static CfEv ev[CF_EVMAX];
static int nev;
static uint32_t salt, ioc;
static const uint8_t *rdp[8];
static uint8_t *wrp[8];

static void log_ev(int t, uint16_t a, uint8_t v) { if (nev < CF_EVMAX) { ev[nev].t = (uint8_t)t; ev[nev].a = a; ev[nev].v = v; nev++; } }
static uint8_t mrd(void *c, uint16_t a) { (void)c; log_ev(0, a, mem[a]); return mem[a]; }
static void mwr(void *c, uint16_t a, uint8_t v) { (void)c; log_ev(1, a, v); mem[a] = v; }
static uint8_t iord(void *c, uint16_t p) { (void)c; uint32_t h = (p * 2654435761u) ^ (salt + (++ioc) * 40503u); h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; log_ev(2, p, (uint8_t)h); return (uint8_t)h; }
static void iowr(void *c, uint16_t p, uint8_t v) { (void)c; log_ev(3, p, v); }

void N(setup)(const CpuState *s, const uint8_t *m, uint32_t iosalt, uint8_t fastmask)
{
    memcpy(mem, m, 65536);
    salt = iosalt; ioc = 0; nev = 0;
    z80_init(&z);
    z.mem_read = mrd; z.mem_write = mwr; z.io_read = iord; z.io_write = iowr;
    z.af.w = s->af; z.bc.w = s->bc; z.de.w = s->de; z.hl.w = s->hl; z.ix.w = s->ix; z.iy.w = s->iy; z.sp.w = s->sp; z.wz.w = s->wz;
    z.af_.w = s->af_; z.bc_.w = s->bc_; z.de_.w = s->de_; z.hl_.w = s->hl_; z.pc = s->pc;
    z.i = s->i; z.r = s->r; z.iff1 = s->iff1; z.iff2 = s->iff2; z.im = s->im; z.halted = s->halted; z.halt_ei = s->halt_ei;
    z.ei_delay = s->ei_delay; z.q = s->q; z.q_prev = s->q_prev; z.r800 = s->r800; z.int_line = s->int_line; z.int_vec = s->int_vec; z.m1_wait = s->m1_wait;
    z.r8c = s->r8c; z.r8_page = s->r8_page; memcpy(z.r8_delay, s->r8_delay, 4); z.extra = s->extra; z.cycles = s->cycles;
    for (int b = 0; b < 8; b++) {
        rdp[b] = mem + b * 8192; wrp[b] = mem + b * 8192;
        z.frd[b] = (fastmask >> b) & 1; z.fwr[b] = (fastmask >> b) & 1;
    }
    z.rdp = rdp; z.wrp = wrp;
}
int N(step)(void) { nev = 0; return z80_step(&z); }
void N(get)(CpuState *s)
{
    memset(s, 0, sizeof *s);
    s->af = z.af.w; s->bc = z.bc.w; s->de = z.de.w; s->hl = z.hl.w; s->ix = z.ix.w; s->iy = z.iy.w; s->sp = z.sp.w; s->wz = z.wz.w;
    s->af_ = z.af_.w; s->bc_ = z.bc_.w; s->de_ = z.de_.w; s->hl_ = z.hl_.w; s->pc = z.pc;
    s->i = z.i; s->r = z.r; s->iff1 = z.iff1; s->iff2 = z.iff2; s->im = z.im; s->halted = z.halted; s->halt_ei = z.halt_ei;
    s->ei_delay = z.ei_delay; s->q = z.q; s->q_prev = z.q_prev; s->r800 = z.r800; s->int_line = z.int_line; s->int_vec = z.int_vec; s->m1_wait = z.m1_wait;
    s->r8c = z.r8c; s->r8_page = z.r8_page; memcpy(s->r8_delay, z.r8_delay, 4); s->extra = z.extra; s->cycles = z.cycles;
}
void N(set_int)(int level, uint8_t vec) { z.int_line = (uint8_t)(level != 0); z.int_vec = vec; }
const uint8_t *N(mem)(void) { return mem; }
int N(nev)(void) { return nev; }
const CfEv *N(ev)(void) { return ev; }
void N(poke)(uint16_t a, uint8_t v) { mem[a] = v; }
static uint64_t run_next;
void N(run)(uint64_t end, uint64_t next)
{
    nev = 0; run_next = next;
#ifdef CF_REF
    { int r = z.r800; do z80_step(&z); while (z.cycles < end && z.cycles < run_next && z.r800 == r); }
#else
    (void)z80_run(&z, end, &run_next);
#endif
}
