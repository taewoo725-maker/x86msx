/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* jobs.h - X86VHD V1.0.0: 복사 일감 만들기와 실제 복사·삭제 (화면과 무관, 진행은 콜백으로 알림) */
#ifndef X86VHD_JOBS_H
#define X86VHD_JOBS_H
#include "winhost.h"

typedef struct { wchar_t source[1024]; VpItem item; } CopyJob;      /* 폴더 일감은 source 가 원래 폴더 */
typedef struct { CopyJob *jobs; int count, cap; uint64_t file_bytes; int too_big; } JobList;

void joblist_free(JobList *list);
/* 끌어다 놓은 경로들을 dest_dir(사진 안 폴더 경로) 아래로 풀어 일감에 더함. 폴더는 먼저, 그 안은 뒤에 */
int joblist_add_sources(JobList *list, const wchar_t *const *paths, int path_count, const char *dest_dir);
void joblist_sort_small_first(JobList *list);                       /* 폴더는 앞에 그대로, 파일은 작은 것부터 */
VpItem *joblist_items(const JobList *list);                        /* 계획 계산용 항목 배열 (free 로 해제) */

typedef struct {
    void *user;
    void (*progress)(void *user, uint64_t bytes_done, uint64_t bytes_total, const char *current_name);
    int (*cancelled)(void *user);                                   /* 1 이면 멈춤 */
} JobCallbacks;
typedef struct { int copied_files, created_dirs, failed; uint64_t copied_bytes; int cancelled; char first_error[256]; } JobReport;

void run_copy(const JobList *list, int count, const JobCallbacks *cb, JobReport *report);
/* paths = 지울 항목의 전체 경로 (UTF-8). 폴더는 안까지. root 밖이면 거절 */
void run_delete(const char *const *paths, int path_count, const char *root, JobReport *report);
#endif
