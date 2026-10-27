/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* schedtest.c - 시간표(sched.h)·인터럽트선 시험 (5.2.0 구조 개편 1단계) */
#include "../host/msx_host.h"
#include <stdio.h>
#include <string.h>
static int pass, fail;
static void ck(const char *n, long got, long want) { if (got == want) pass++; else { fail++; printf("FAIL %-40s %ld (want %ld)\n", n, got, want); } }
int main(int argc, char **argv)
{
    Sched s; sched_init(&s);
    ck("empty next = max", s.next == UINT64_MAX, 1);
    ck("empty pop", sched_pop(&s, 1000), -1);
    sched_set(&s, 5, 300); sched_set(&s, 2, 100); sched_set(&s, 7, 100);
    ck("next = earliest", (long)s.next, 100);
    ck("not yet", sched_pop(&s, 99), -1);
    ck("same time: lower id first", sched_pop(&s, 100), 2);
    ck("then next id", sched_pop(&s, 100), 7);
    ck("later not due", sched_pop(&s, 100), -1);
    ck("next moves", (long)s.next, 300);
    sched_cancel(&s, 5);
    ck("cancel empties", s.next == UINT64_MAX, 1);
    sched_set(&s, 3, 500); sched_set(&s, 3, 200);
    ck("re-set replaces", (long)s.next, 200);
    ck("re-set pops once", sched_pop(&s, 1000), 3);
    ck("re-set gone", sched_pop(&s, 1000), -1);
    ck("pending", (sched_set(&s, 9, 10), sched_pending(&s, 9)), 1);

    /* 기계에 붙여서: 명령 고리에서 정확히 한 번 불림 */
    static MSX m; char err[256];
    const char *prof = argc > 1 ? argv[1] : "machines/C-BIOS";
    if (msx_host_init(&m, prof, err, sizeof err)) { printf("init: %s\n", err); return 1; }
    msx_run_frame(&m);
    msx_sched_test(&m, m.cpu.cycles + 1000);
    msx_run_frame(&m);
    ck("machine: event fired once", (long)m.sched_test_hits, 1);
    msx_sched_test(&m, m.cpu.cycles + 100000);          /* 다음 프레임 안 */
    msx_reset(&m);
    msx_run_frame(&m); msx_run_frame(&m);
    ck("machine: reset cancels", (long)m.sched_test_hits, 1);
    msx_irq_set(&m, IRQ_COUNT - 1, 1);
    ck("irq line raises INT", (long)(m.irq_lines != 0), 1);
    msx_irq_set(&m, IRQ_COUNT - 1, 0);
    ck("irq line lowers", (long)((m.irq_lines >> (IRQ_COUNT - 1)) & 1), 0);
    printf("schedtest: %d/%d passed\n", pass, pass + fail);
    return fail != 0;
}
