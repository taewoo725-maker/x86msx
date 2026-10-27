/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * corefuzz.c - Z80/R800 코어 동등성 시험 (5.2.152, 7단계의 안전망)
 *
 * 현재 코어(src/z80.c)와 동결한 기준 코어(tests/ref/z80_ref.c, 5.2.151)에 같은 무작위 상태·같은 무작위 프로그램을 넣고
 * 명령 하나마다 견준다: 모든 레지스터·숨은 상태(WZ·Q·R·IFF·EI 지연·HALT), 소비 T-state, 메모리 읽기/쓰기와 I/O 순서·값,
 * (R800 일 때) R800 클럭. 인터럽트 선(IM 0/1/2)을 무작위로 켜고 끔. 현재 코어는 일부 시험에서 메모리 빠른 길(rdp/wrp)로도 돌려
 * 콜백 길과 같은지 본다. 명령 종류가 얼마나 걸렸는지(주 명령·CB·ED·DD·FD·DDCB·FDCB) 세어 보여 준다.
 *
 * 사용: corefuzz [시드 수=40] [시드당 명령 수=20000] [첫 시드=1]      처음 다른 곳에서 멈춤(종료 코드 1)
 */
#include "cf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rs;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }
static uint16_t rnd16(void)
{
    static const uint16_t edge[] = { 0, 1, 0xFF, 0x100, 0x7F, 0x80, 0x7FFF, 0x8000, 0xFFFF, 0xFF00, 0x0F, 0x10, 0x8F, 0x00FF, 0x4000 };
    uint32_t r = rnd();
    return (r & 3) == 0 ? edge[(r >> 2) % (sizeof edge / sizeof *edge)] : (uint16_t)(rnd() ^ (rnd() << 7));
}

static uint8_t seen[7][256];
static const char *cls_name[7] = { "주", "CB", "ED", "DD", "FD", "DDCB", "FDCB" };
static void note(const uint8_t *m, uint16_t pc)
{
    uint8_t b0 = m[pc], b1 = m[(uint16_t)(pc + 1)], b3 = m[(uint16_t)(pc + 3)];
    int k = 0; uint8_t o = b0;
    if (b0 == 0xCB) { k = 1; o = b1; }
    else if (b0 == 0xED) { k = 2; o = b1; }
    else if (b0 == 0xDD || b0 == 0xFD) {
        int base = b0 == 0xDD ? 3 : 4;
        if (b1 == 0xCB) { k = base + 2; o = b3; } else if (b1 == 0xDD || b1 == 0xFD || b1 == 0xED) { return; } else { k = base; o = b1; }
    }
    seen[k][o] = 1;
}

static void fill(uint8_t *m, int mode)
{
    static const uint8_t pre[] = { 0xCB, 0xDD, 0xED, 0xFD, 0xDD, 0xFD, 0xCB };
    for (int i = 0; i < 65536; i++) {
        uint32_t r = rnd();
        if (mode == 0) m[i] = (uint8_t)r;                                        /* 순수 무작위 */
        else if (mode == 1) m[i] = (r & 7) == 0 ? pre[(r >> 3) % sizeof pre] : (uint8_t)(r >> 8);     /* 접두 많이 */
        else m[i] = (r & 3) ? (uint8_t)(r >> 8) : (uint8_t)((r >> 8) & 0x3F);     /* 낮은 명령 쪽 (LD·INC·DEC·산술 등) */
    }
}

/* 지시 모드: 명령 종류를 고르게 골라 명령 하나를 PC 자리에 직접 써 넣음 (무작위 바이트로는 DDCB·ED 가 거의 안 나옴) */
static int gen_insn(uint8_t *o)
{
    uint32_t r = rnd() % 100; int n = 0, cls;
    if (r < 22) cls = 0; else if (r < 34) cls = 1; else if (r < 48) cls = 2; else if (r < 66) cls = 3; else if (r < 84) cls = 4; else if (r < 92) cls = 5; else cls = 6;
    uint8_t op = (uint8_t)rnd();
    if (cls == 0) { while (op == 0xCB || op == 0xDD || op == 0xED || op == 0xFD) op = (uint8_t)rnd(); }
    uint32_t chain = rnd() % 20;                                   /* 접두 겹치기 (DD DD FD ... ) 가끔 */
    if (cls >= 3 && chain == 0) { int k = 1 + (int)(rnd() % 3); while (k--) o[n++] = (rnd() & 1) ? 0xDD : 0xFD; }
    switch (cls) {
    case 0: o[n++] = op; break;
    case 1: o[n++] = 0xCB; o[n++] = op; break;
    case 2: o[n++] = 0xED; o[n++] = op; break;
    case 3: o[n++] = 0xDD; o[n++] = (op == 0xCB) ? 0xCB : op; if (op == 0xDD || op == 0xFD || op == 0xED) o[n - 1] = 0x21; break;
    case 4: o[n++] = 0xFD; o[n++] = (op == 0xCB) ? 0xCB : op; if (op == 0xDD || op == 0xFD || op == 0xED) o[n - 1] = 0x21; break;
    case 5: o[n++] = 0xDD; o[n++] = 0xCB; o[n++] = (uint8_t)rnd(); o[n++] = op; break;
    default: o[n++] = 0xFD; o[n++] = 0xCB; o[n++] = (uint8_t)rnd(); o[n++] = op; break;
    }
    if (n == 2 && o[1] == 0xCB && cls >= 3) { o[n++] = (uint8_t)rnd(); o[n++] = (uint8_t)rnd(); }   /* DD CB 쓰레기 → DDCB 모양으로 */
    for (int i = 0; i < 3; i++) o[n + i] = (uint8_t)rnd();         /* 인수 바이트 (뒤에 이어 쓴 것도 무작위) */
    return n + 3;
}

static void rnd_state(CpuState *s, int r800)
{
    memset(s, 0, sizeof *s);
    s->af = rnd16(); s->bc = rnd16(); s->de = rnd16(); s->hl = rnd16(); s->ix = rnd16(); s->iy = rnd16(); s->sp = rnd16(); s->wz = rnd16();
    s->af_ = rnd16(); s->bc_ = rnd16(); s->de_ = rnd16(); s->hl_ = rnd16(); s->pc = rnd16();
    s->i = (uint8_t)rnd(); s->r = (uint8_t)rnd(); s->iff1 = rnd() & 1; s->iff2 = rnd() & 1; s->im = (uint8_t)(rnd() % 3);
    s->halted = 0; s->halt_ei = (rnd() & 7) == 0; s->ei_delay = (rnd() & 7) == 0; s->q = (uint8_t)rnd(); s->q_prev = (uint8_t)rnd();
    s->r800 = (uint8_t)r800; s->int_line = 0; s->int_vec = (rnd() & 1) ? 0xFF : (uint8_t)rnd(); s->m1_wait = rnd() & 1;
    s->r8c = 0; s->r8_page = -1; for (int i = 0; i < 4; i++) s->r8_delay[i] = (uint8_t)(rnd() % 3);
    s->extra = 0; s->cycles = 0;
}

#define F(x) if (a->x != b->x) { printf("    %-9s 현재 %llX  기준 %llX\n", #x, (unsigned long long)a->x, (unsigned long long)b->x); n++; }
static int diff_state(const CpuState *a, const CpuState *b)
{
    int n = 0;
    F(af) F(bc) F(de) F(hl) F(ix) F(iy) F(sp) F(wz) F(af_) F(bc_) F(de_) F(hl_) F(pc)
    F(i) F(r) F(iff1) F(iff2) F(im) F(halted) F(halt_ei) F(ei_delay) F(q) F(q_prev) F(r800) F(int_line) F(int_vec) F(m1_wait)
    F(r8c) F(r8_page) F(extra) F(cycles)
    for (int i = 0; i < 4; i++) if (a->r8_delay[i] != b->r8_delay[i]) { printf("    r8_delay[%d]\n", i); n++; }
    return n;
}

int main(int argc, char **argv)
{
    int seeds = argc > 1 ? atoi(argv[1]) : 40, steps = argc > 2 ? atoi(argv[2]) : 20000, first = argc > 3 ? atoi(argv[3]) : 1;
    static uint8_t m0[65536];
    long total = 0, ints = 0;
    for (int sd = first; sd < first + seeds; sd++) {
        rs = 0x9E3779B97F4A7C15ull * (uint64_t)(sd + 1); for (int i = 0; i < 8; i++) rnd();
        int r800 = (sd % 5) == 4, mode = sd % 5 < 3 ? sd % 3 : 3;      /* 모드 3 = 지시 모드 */
        uint8_t fast = (sd % 4 == 3) ? (uint8_t)rnd() : 0;                 /* 현재 코어만 빠른 길 (4 시드마다 1) */
        CpuState s0; rnd_state(&s0, r800);
        fill(m0, mode);
        uint32_t salt = rnd();
        cur_setup(&s0, m0, salt, fast);
        ref_setup(&s0, m0, salt, 0);
        int run_mode = (sd % 3) != 1;                                       /* 3 시드 중 2 시드는 z80_run 도 섞어 씀 */
        int intp = (sd % 2) ? 40 : 400;                                     /* 인터럽트 선 켜고 끄는 빈도 */
        for (int st = 0; st < steps; st++) {
            CpuState pa, pb; cur_get(&pa); ref_get(&pb);
            uint8_t bytes[4]; for (int i = 0; i < 4; i++) bytes[i] = ref_mem()[(uint16_t)(pb.pc + i)];
            if (mode != 3) note(ref_mem(), pb.pc);
            if (mode == 3) {                                                          /* 지시 모드: 이번 PC 에 새 명령 */
                uint8_t g[12]; int gl = gen_insn(g);
                for (int i = 0; i < gl; i++) { cur_poke((uint16_t)(pb.pc + i), g[i]); ref_poke((uint16_t)(pb.pc + i), g[i]); }
                for (int i = 0; i < 4; i++) bytes[i] = g[i];
                note(ref_mem(), pb.pc);
            }
            if (rnd() % (uint32_t)intp == 0) { int lv = !pb.int_line; uint8_t v = (rnd() & 1) ? 0xFF : (uint8_t)rnd(); cur_set_int(lv, v); ref_set_int(lv, v); ints++; }
            int ta, tb;
            if (run_mode && (st & 1)) {                      /* 이어 실행(z80_run) 시험: 한 번에 여러 명령, 끝 시각·예약 시각 무작위 */
                uint64_t c0 = pb.cycles, end = c0 + 1 + rnd() % 300, nx = (rnd() & 1) ? UINT64_MAX : c0 + 1 + rnd() % 200;
                cur_run(end, nx); ref_run(end, nx);
                CpuState qa, qb; cur_get(&qa); ref_get(&qb);
                ta = (int)(qa.cycles - c0); tb = (int)(qb.cycles - c0);
            } else { ta = cur_step(); tb = ref_step(); }
            CpuState a, b; cur_get(&a); ref_get(&b);
            int bad = 0;
            printf("%s", "");
            if (ta != tb) bad |= 1;
            if (memcmp(&a, &b, sizeof a)) bad |= 2;
            /* 이벤트: 빠른 길로 간 메모리 칸은 현재 코어에 이벤트가 없으니 양쪽에서 빼고 견줌 */
            CfEv ea[CF_EVMAX], eb[CF_EVMAX]; int na = 0, nb = 0;
            for (int i = 0; i < cur_nev(); i++) { const CfEv *e = &cur_ev()[i]; if (e->t < 2 && ((fast >> (e->a >> 13)) & 1)) continue; ea[na++] = *e; }
            for (int i = 0; i < ref_nev(); i++) { const CfEv *e = &ref_ev()[i]; if (e->t < 2 && ((fast >> (e->a >> 13)) & 1)) continue; eb[nb++] = *e; }
            if (na != nb || memcmp(ea, eb, sizeof(CfEv) * (size_t)na)) bad |= 4;
            if (fast ? ((st & 31) == 31 || st == steps - 1) : 0) { if (memcmp(cur_mem(), ref_mem(), 65536)) bad |= 8; }
            else if (!fast && (bad & 4) == 0 && (st & 255) == 255) { if (memcmp(cur_mem(), ref_mem(), 65536)) bad |= 8; }
            if (bad) {
                printf("다름! 시드 %d, 명령 %d (r800=%d fast=%02X 모드 %d)  PC=%04X 바이트 %02X %02X %02X %02X  T 현재 %d 기준 %d  (종류 %d)\n",
                       sd, st, r800, fast, mode, pb.pc, bytes[0], bytes[1], bytes[2], bytes[3], ta, tb, bad);
                diff_state(&a, &b);
                if (bad & 4) {
                    printf("    이벤트 현재 %d개, 기준 %d개\n", na, nb);
                    for (int i = 0; i < (na > nb ? na : nb); i++) {
                        if (i < na) printf("      현재 %d %04X %02X", ea[i].t, ea[i].a, ea[i].v); else printf("      현재 -");
                        if (i < nb) printf("   | 기준 %d %04X %02X\n", eb[i].t, eb[i].a, eb[i].v); else printf("   | 기준 -\n");
                    }
                }
                if (bad & 8) { for (int i = 0; i < 65536; i++) if (cur_mem()[i] != ref_mem()[i]) { printf("    메모리 %04X 현재 %02X 기준 %02X\n", i, cur_mem()[i], ref_mem()[i]); break; } }
                printf("  이전 상태: AF=%04X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X WZ=%04X IM%d IFF%d%d I=%02X R=%02X\n",
                       pb.af, pb.bc, pb.de, pb.hl, pb.ix, pb.iy, pb.sp, pb.wz, pb.im, pb.iff1, pb.iff2, pb.i, pb.r);
                return 1;
            }
            total++;
        }
    }
    int cov = 0, all = 0;
    for (int k = 0; k < 7; k++) { int c = 0; for (int i = 0; i < 256; i++) c += seen[k][i]; cov += c; all += 256; printf("%s %d  ", cls_name[k], c); }
    printf("\ncorefuzz: %ld 명령 × (현재 코어 = 기준 코어) 모두 같음 — 시드 %d~%d, 명령 종류 %d/%d 걸림, 인터럽트 선 바꿈 %ld번\n", total, first, first + seeds - 1, cov, all, ints);
    return 0;
}
