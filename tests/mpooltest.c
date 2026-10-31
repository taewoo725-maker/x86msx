/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* mpooltest - src/mpool.h 일꾼 풀 시험 (PC 스레드로 일꾼 흉내). 일꾼이 한 결과 = 메인이 직접 한 결과인지, 일이 넘치면 -1 인지, 멈추고 다시 띄워도 되는지 */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/mpool.h"

static int pass, fail;
#define CHECK(c, m) do { if (c) pass++; else { fail++; printf("FAIL: %s (line %d)\n", m, __LINE__); } } while (0)

typedef struct { uint64_t a, b, out; } Job;
static void job_sum(void *p) { Job *j = (Job *)p; volatile uint64_t s = 0; for (uint64_t i = j->a; i < j->b; i++) s += i * 2654435761u ^ (i >> 3); j->out = s; }
static uint64_t inline_sum(uint64_t a, uint64_t b) { Job j = { a, b, 0 }; job_sum(&j); return j.out; }
static void job_slow(void *p) { Job *j = (Job *)p; for (volatile int i = 0; i < 2000000; i++) ; j->out = 7; }

static pthread_t th[MP_MAXW]; static int nth;
static void *thr(void *a) { mpool_worker(a); return 0; }
static void start_workers(int n)
{
    mpool_prepare(n); nth = n;
    for (int i = 0; i < n; i++) { mp_slots[i].state = MPS_OFF; pthread_create(&th[i], 0, thr, &mp_slots[i]); }
    for (int w = 0; w < 100000 && mpool_idle() < n; w++) sched_yield();
}
static void stop_workers(void) { mpool_stop_all(); for (int i = 0; i < nth; i++) pthread_join(th[i], 0); }

int main(void)
{
    /* 1. 일꾼이 없으면 -1 */
    mpool_prepare(0);
    Job j0 = { 0, 10, 0 };
    CHECK(mpool_submit(job_sum, &j0) == -1, "no worker -> -1");

    for (int W = 1; W <= 8; W++) {
        start_workers(W);
        CHECK(mpool_workers() == W && mpool_idle() == W, "workers up");
        /* 2. 일꾼 수만큼 한꺼번에 올리고, 하나 더는 거절 */
        static Job jobs[16]; int id[16];
        for (int i = 0; i < W; i++) { jobs[i].a = 1000u * i; jobs[i].b = jobs[i].a + 200000; jobs[i].out = 0; id[i] = mpool_submit(job_slow, &jobs[i]); CHECK(id[i] >= 0, "submit ok"); }
        Job extra = { 0, 5, 0 };
        CHECK(mpool_submit(job_sum, &extra) == -1, "full -> -1");
        for (int i = 0; i < W; i++) { while (!mpool_done(id[i])) mp_cpu_pause(); CHECK(jobs[i].out == 7, "slow job done"); }
        /* 3. 결과가 직접 한 것과 같은지 (여러 번) */
        for (int round = 0; round < 200; round++) {
            for (int i = 0; i < W; i++) { jobs[i].a = (uint64_t)(rand() % 100000); jobs[i].b = jobs[i].a + (uint64_t)(rand() % 50000); jobs[i].out = 0; id[i] = mpool_submit(job_sum, &jobs[i]); }
            for (int i = 0; i < W; i++) { if (id[i] < 0) continue; while (!mpool_done(id[i])) mp_cpu_pause(); CHECK(jobs[i].out == inline_sum(jobs[i].a, jobs[i].b), "same as inline"); }
        }
        uint64_t total = 0; for (int i = 0; i < W; i++) total += mp_slots[i].ran;
        CHECK(total >= (uint64_t)W * 201, "ran count");
        /* 4. 멈추고 완전히 끝나야 함, 다시 띄워도 됨 */
        stop_workers();
        CHECK(mpool_all_off(), "all off after stop");
    }
    printf("mpooltest: %d/%d passed\n", pass, pass + fail);
    return fail ? 1 : 0;
}
