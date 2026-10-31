/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* mpool.h - 8.0.1 다중CPU판: 일꾼 코어 풀 (일 목록 + 놀고 있는 일꾼이 가져가기)
 *
 * 원칙: 쓸 수 있는 코어는 모두 쓴다. 단 MSX 본체(Z80·VDP·타이밍)는 메인 코어에 둔다.
 * - 일꾼은 칸(MpSlot) 하나씩 가진다. 메인이 놀고 있는 칸에 일(fn, arg)을 올리면 그 일꾼이 한다.
 * - 일꾼은 펌웨어 함수·파일·USB 를 부르지 않는다 (계산만).
 * - 일꾼이 없거나 모두 바쁘면 mpool_submit 이 -1 을 돌려주고 메인이 직접 한다 (최악이어도 지금 속도).
 * 이 파일은 헤더만으로 동작한다 (UEFI·PC 시험 공용). 일꾼 코어를 띄우는 방법만 플랫폼이 다르다:
 *   UEFI = MP Services StartupThisAP(mpool_worker, 칸), PC 시험 = 스레드. */
#ifndef MPOOL_H
#define MPOOL_H
#include <stdint.h>

#define MP_MAXW 64
enum { MPS_OFF = 0, MPS_IDLE = 1, MPS_POSTED = 2, MPS_RUN = 3, MPS_DONE = 4 };
typedef void (*MpJobFn)(void *arg);

typedef struct MpSlot {
    volatile uint32_t state;      /* MPS_*: 메인과 일꾼이 번갈아 바꿈 (OFF→IDLE 일꾼, IDLE→POSTED 메인, POSTED→RUN→DONE 일꾼, DONE→IDLE 메인) */
    volatile uint32_t stop;       /* 1 이면 일꾼이 끝냄 */
    MpJobFn fn;
    void *arg;
    volatile uint64_t ran;        /* 이 일꾼이 한 일 수 */
    volatile uint64_t beat;       /* 일꾼이 기다리며 도는 횟수 (살아 있는지 확인용) */
    volatile uint64_t busy[2];    /* 8.0.34: 일한 시간(TSC 누적) — [0] 화면 등 그 밖의 일, [1] mp_role1 에 해당하는 일(MIDI 합성). 시스템 보기 > 성능이 읽음 */
    uint8_t pad[8];               /* 칸마다 캐시 줄(64바이트)을 따로 써서 서로 방해하지 않게 */
} __attribute__((aligned(64))) MpSlot;

typedef struct { uint64_t submitted, rejected; } MpStats;

__attribute__((unused)) static MpSlot mp_slots[MP_MAXW];
__attribute__((unused)) static MpStats mp_stats;
__attribute__((unused)) static const void *mp_role1;   /* 8.0.34: 이 함수가 올린 일은 busy[1] 에 모음 (없으면 모두 busy[0]) */
#if defined(__x86_64__) || defined(__i386__)
static inline uint64_t mp_tsc(void) { return __builtin_ia32_rdtsc(); }
#else
static inline uint64_t mp_tsc(void) { return 0; }
#endif

#if defined(__GNUC__) || defined(__clang__)
#define MP_LOAD(p)    __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define MP_STORE(p,v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#endif
#if defined(__x86_64__) || defined(__i386__)
static inline void mp_cpu_pause(void) { __asm__ volatile("pause"); }
#else
static inline void mp_cpu_pause(void) { }
#endif

#ifdef MPOOL_MWAIT
__attribute__((unused)) static volatile int mp_mwait_ok;   /* 8.0.23: CPU 가 MONITOR/MWAIT 를 지원하고 플랫폼이 켰으면 1 (UEFI 일꾼 코어만, 링 0) */
#endif
/* 일꾼 코어에서 돌리는 함수. arg = 그 일꾼의 MpSlot. stop 이 1 이 될 때까지 돌다가 돌아온다 */
__attribute__((unused)) static void mpool_worker(void *arg)
{
    MpSlot *s = (MpSlot *)arg;
    MP_STORE(&s->state, MPS_IDLE);
    for (;;) {
        if (MP_LOAD(&s->stop)) break;
        if (MP_LOAD(&s->state) == MPS_POSTED) {
            MP_STORE(&s->state, MPS_RUN);
            { uint64_t t0 = mp_tsc(); s->fn(s->arg); s->busy[(const void *)s->fn == mp_role1 && mp_role1 ? 1 : 0] += mp_tsc() - t0; }
            s->ran++;
            MP_STORE(&s->state, MPS_DONE);
        } else {
            s->beat++;
#ifdef MPOOL_MWAIT
            if (mp_mwait_ok) {                    /* 8.0.23: 칸(64바이트 한 줄)을 지켜보다 잠듦 — 메인이 state·stop 을 쓰면 깸 (발열·전력) */
                __asm__ volatile("monitor" : : "a"((const void *)s), "c"(0), "d"(0) : "memory");
                if (MP_LOAD(&s->state) != MPS_POSTED && !MP_LOAD(&s->stop)) __asm__ volatile("mwait" : : "a"(0), "c"(0) : "memory");
                continue;
            }
#endif
            mp_cpu_pause();
        }
    }
    MP_STORE(&s->state, MPS_OFF);
}

/* 칸 하나를 쓸 준비 (일꾼을 띄우기 전에). n 개 */
__attribute__((unused)) static void mpool_prepare(int n)
{
    if (n > MP_MAXW) n = MP_MAXW;
    for (int i = 0; i < MP_MAXW; i++) { mp_slots[i].state = MPS_OFF; mp_slots[i].stop = 0; mp_slots[i].fn = 0; mp_slots[i].arg = 0; mp_slots[i].ran = 0; mp_slots[i].busy[0] = mp_slots[i].busy[1] = 0; }
    mp_stats.submitted = mp_stats.rejected = 0;
    (void)n;
}

/* 일꾼이 놀고 있으면 일을 올리고 칸 번호를 돌려줌, 모두 바쁘거나 없으면 -1 (메인이 직접 해야 함) */
__attribute__((unused)) static int mpool_submit(MpJobFn fn, void *arg)
{
    for (int i = 0; i < MP_MAXW; i++) {
        if (MP_LOAD(&mp_slots[i].state) == MPS_IDLE) {
            mp_slots[i].fn = fn; mp_slots[i].arg = arg;
            MP_STORE(&mp_slots[i].state, MPS_POSTED);
            mp_stats.submitted++;
            return i;
        }
    }
    mp_stats.rejected++;
    return -1;
}

/* 올린 일이 끝났으면 1 (칸을 다시 놀게 만듦). 아직이면 0 */
__attribute__((unused)) static int mpool_done(int idx)
{
    if (idx < 0 || idx >= MP_MAXW) return 0;
    if (MP_LOAD(&mp_slots[idx].state) != MPS_DONE) return 0;
    MP_STORE(&mp_slots[idx].state, MPS_IDLE);
    return 1;
}

/* 지금 일꾼 수 (OFF 가 아닌 칸) / 놀고 있는 일꾼 수 */
__attribute__((unused)) static int mpool_workers(void) { int n = 0; for (int i = 0; i < MP_MAXW; i++) if (MP_LOAD(&mp_slots[i].state) != MPS_OFF) n++; return n; }
__attribute__((unused)) static int mpool_idle(void) { int n = 0; for (int i = 0; i < MP_MAXW; i++) if (MP_LOAD(&mp_slots[i].state) == MPS_IDLE) n++; return n; }

/* 모든 일꾼에게 끝내라고 함 (바쁜 일꾼은 하던 일을 마친 뒤 끝냄). 끝난 일꾼 수를 돌려줌 */
__attribute__((unused)) static void mpool_stop_all(void) { for (int i = 0; i < MP_MAXW; i++) MP_STORE(&mp_slots[i].stop, 1); }
__attribute__((unused)) static int mpool_all_off(void) { for (int i = 0; i < MP_MAXW; i++) if (MP_LOAD(&mp_slots[i].state) != MPS_OFF) return 0; return 1; }

#endif
