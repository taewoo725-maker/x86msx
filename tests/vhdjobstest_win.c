/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* vhdjobstest_win.c - X86VHD 복사·삭제 처리부(jobs.c)를 Windows(또는 Wine)에서 실제 파일로 시험하는 콘솔 프로그램
 *   vhdjobstest.exe <가상하드 폴더> <넣을 원본 폴더> <MB> */
#include "../tools/x86vhd/jobs.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int progress_calls;
static void on_progress(void *u, uint64_t done, uint64_t total, const char *name) { (void)u; (void)done; (void)total; (void)name; progress_calls++; }
static int never_cancel(void *u) { (void)u; return 0; }

int wmain(int argc, wchar_t **argv)
{
    if (argc < 4) { printf("usage\n"); return 2; }
    char root[VP_PATH]; wide_to_text(argv[1], root, sizeof root);
    uint32_t size_mb = (uint32_t)_wtoi(argv[3]);
    const wchar_t *source = argv[2];

    VpTree tree; vp_init(&tree, root);
    CHECK(vp_scan(&tree, windows_host()) == 0, "scan");
    VpFit fit; vp_check(&tree, size_mb, VF_FAT16, &fit);
    printf("before: used %u KB / %u KB fits=%d\n", fit.used_kb, fit.cap_kb, fit.fits);

    JobList jobs; memset(&jobs, 0, sizeof jobs);
    joblist_add_sources(&jobs, &source, 1, root);
    printf("jobs %d, bytes %llu\n", jobs.count, (unsigned long long)jobs.file_bytes);
    CHECK(jobs.count > 2 && jobs.jobs[0].item.is_dir, "first job should be the folder");

    VpItem *items = joblist_items(&jobs);
    VpFit all; int k = vp_plan_fit(&tree, items, jobs.count, size_mb, VF_FAT16, &all);
    printf("plan: %d of %d fit (all fits=%d need=%u MB)\n", k, jobs.count, all.fits, all.need_mb);
    free(items);

    JobCallbacks cb = { NULL, on_progress, never_cancel };
    JobReport rep; run_copy(&jobs, k, &cb, &rep);
    printf("copied files %d dirs %d failed %d bytes %llu err=[%s]\n", rep.copied_files, rep.created_dirs, rep.failed, (unsigned long long)rep.copied_bytes, rep.first_error);
    CHECK(rep.failed == 0 && rep.copied_files > 0 && progress_calls > 0, "copy result");

    VpTree after; vp_init(&after, root); vp_scan(&after, windows_host());
    VpFit fit_after; vp_check(&after, size_mb, VF_FAT16, &fit_after);
    printf("after: used %u KB fits=%d (need %u MB)\n", fit_after.used_kb, fit_after.fits, fit_after.need_mb);
    CHECK(k == jobs.count ? fit_after.fits : 1, "all-fit plan must leave a mountable folder");
    if (k < jobs.count) CHECK(fit_after.fits, "partial plan must still fit after real copy");

    /* 지우기: 방금 만든 최상위 폴더 (범위 안) 와 범위 밖 거절 */
    char target[VP_PATH + 300]; snprintf(target, sizeof target, "%s\\%s", root, jobs.jobs[0].item.name);
    const char *bad[] = { "C:\\Windows" };
    JobReport del; run_delete(bad, 1, root, &del);
    CHECK(del.failed == 1, "out-of-root delete must be refused");
    const char *good[] = { target };
    run_delete(good, 1, root, &del);
    printf("deleted files %d dirs %d failed %d\n", del.copied_files, del.created_dirs, del.failed);
    CHECK(del.failed == 0 && del.copied_files > 0, "delete result");
    VpTree gone; vp_init(&gone, root); vp_scan(&gone, windows_host());
    CHECK(gone.count == tree.count, "back to original count %d vs %d", gone.count, tree.count);

    joblist_free(&jobs); vp_free(&tree); vp_free(&after); vp_free(&gone);
    printf(failures ? "vhdjobstest: %d FAIL\n" : "vhdjobstest: OK\n", failures);
    return failures != 0;
}
