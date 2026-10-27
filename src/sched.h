/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * sched.h - 시간표 (구조 개편 1단계, 5.2.0) — 뼈대: 바꾸지 않음
 *
 * 시계는 Z80 클럭(3.579545 MHz, m->cpu.cycles) 하나. 장치는 "이 시각에 불러 줘"를 예약하고(sched_set),
 * CPU 명령 고리는 명령마다 now >= next 한 번만 비교한다 (예약이 없으면 next = 최댓값).
 * 사건은 번호(SchedEvent)로만 가리킴 — 함수 포인터를 상태 저장에 넣지 않기 위해. 번호 → 처리 함수 표는 msx.c 에.
 * 같은 시각이면 번호가 작은 사건이 먼저 (결정성). 처리 함수 안에서 다시 예약해도 됨.
 */
#ifndef SCHED_H
#define SCHED_H
#include <stdint.h>

enum SchedEvent {          /* 새 장치는 끝에 번호를 더함 (앞 번호는 바꾸지 않음 — 상태 저장 호환) */
    EV_TEST = 0,           /* 시험용 (tests/schedtest.c) */
    EV_COUNT = 32
};

typedef struct Sched {
    uint64_t when[EV_COUNT];
    uint32_t armed;        /* 비트 = 예약된 사건 */
    uint64_t next;         /* 가장 이른 예약 시각 (없으면 UINT64_MAX) */
} Sched;

static inline void sched_recalc(Sched *s)
{
    uint64_t n = UINT64_MAX;
    for (uint32_t a = s->armed; a; a &= a - 1) { int i = __builtin_ctz(a); if (s->when[i] < n) n = s->when[i]; }
    s->next = n;
}
static inline void sched_init(Sched *s) { for (int i = 0; i < EV_COUNT; i++) s->when[i] = 0; s->armed = 0; s->next = UINT64_MAX; }
static inline void sched_set(Sched *s, int ev, uint64_t when) { s->when[ev] = when; s->armed |= 1u << ev; if (when < s->next) s->next = when; }
static inline void sched_cancel(Sched *s, int ev) { if (s->armed & (1u << ev)) { s->armed &= ~(1u << ev); sched_recalc(s); } }
static inline int sched_pending(const Sched *s, int ev) { return (s->armed >> ev) & 1; }
/* now 까지 된 사건 하나를 꺼냄 (없으면 -1). 꺼낸 사건은 예약이 풀림. 같은 시각이면 번호가 작은 것부터 */
static inline int sched_pop(Sched *s, uint64_t now)
{
    if (now < s->next) return -1;
    int best = -1; uint64_t bw = UINT64_MAX;
    for (uint32_t a = s->armed; a; a &= a - 1) { int i = __builtin_ctz(a); if (s->when[i] <= now && s->when[i] < bw) { bw = s->when[i]; best = i; } }
    if (best >= 0) { s->armed &= ~(1u << best); sched_recalc(s); }
    return best;
}
#endif
