/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* corefuzz 공용: CPU 상태(코어 모양과 무관) + 코어 감싸개 인터페이스 (5.2.152)
 * 같은 감싸개(cf_wrap.c)를 코어마다 한 번씩 컴파일 (CF_PFX = cur_ / ref_), 두 코어를 한 명령씩 같은 입력으로 돌려 견준다. */
#ifndef CF_H
#define CF_H
#include <stdint.h>
typedef struct {
    uint16_t af, bc, de, hl, ix, iy, sp, wz, af_, bc_, de_, hl_, pc;
    uint8_t i, r, iff1, iff2, im, halted, halt_ei, ei_delay, q, q_prev, r800, int_line, int_vec, m1_wait;
    uint32_t r8c; int r8_page; uint8_t r8_delay[4];
    int extra; uint64_t cycles;
} CpuState;
typedef struct { uint8_t t; uint16_t a; uint8_t v; } CfEv;     /* t: 0 메모리 읽기, 1 쓰기, 2 IN, 3 OUT */
#define CF_EVMAX 2048
#define CF_DECL(P) \
    void P##setup(const CpuState *s, const uint8_t *mem, uint32_t iosalt, uint8_t fastmask); \
    int P##step(void); \
    void P##get(CpuState *s); \
    void P##set_int(int level, uint8_t vec); \
    const uint8_t *P##mem(void); \
    int P##nev(void); \
    const CfEv *P##ev(void); \
    void P##poke(uint16_t a, uint8_t v); \
    void P##run(uint64_t end, uint64_t next);
CF_DECL(cur_)
CF_DECL(ref_)
#endif
