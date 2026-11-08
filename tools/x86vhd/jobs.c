/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* jobs.c - X86VHD V1.0.0 복사 일감과 실제 복사·삭제. 설명은 jobs.h */
#include "jobs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FOLDER_DEPTH 24

/* ---------- 일감 목록 ---------- */
void joblist_free(JobList *list) { free(list->jobs); memset(list, 0, sizeof *list); }
static CopyJob *append_job(JobList *list)
{
    if (list->count == list->cap) {
        int ncap = list->cap ? list->cap * 2 : 128;
        CopyJob *p = realloc(list->jobs, sizeof(CopyJob) * (size_t)ncap);
        if (!p) return NULL;
        list->jobs = p; list->cap = ncap;
    }
    CopyJob *job = &list->jobs[list->count++];
    memset(job, 0, sizeof *job);
    return job;
}
static const wchar_t *base_name(const wchar_t *path)
{
    const wchar_t *name = path, *p;
    for (p = path; *p; p++) if (*p == L'\\' || *p == L'/') name = p + 1;
    return name;
}
static void add_entry(JobList *list, const wchar_t *source, const char *dest_dir, const char *name, uint32_t size, int is_dir)
{
    CopyJob *job = append_job(list);
    if (!job) return;
    wcsncpy(job->source, source, 1023);
    snprintf(job->item.dest_dir, sizeof job->item.dest_dir, "%s", dest_dir);
    snprintf(job->item.name, sizeof job->item.name, "%s", name);
    job->item.size = size; job->item.is_dir = is_dir;
    if (!is_dir) list->file_bytes += size;
}
static void add_tree(JobList *list, const wchar_t *source, const char *dest_dir, int depth)
{
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(source, GetFileExInfoStandard, &info)) return;
    char name[256]; wide_to_text(base_name(source), name, sizeof name);
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        uint32_t size = info.nFileSizeHigh ? 0xFFFFFFFFu : info.nFileSizeLow;
        if (info.nFileSizeHigh) list->too_big++;
        add_entry(list, source, dest_dir, name, size, 0);
        return;
    }
    if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || depth >= MAX_FOLDER_DEPTH) return;   /* 바로가기 폴더는 따라가지 않음 (끝없는 되돌이 방지) */
    add_entry(list, source, dest_dir, name, 0, 1);
    char inner[VP_PATH]; snprintf(inner, sizeof inner, "%s\\%s", dest_dir, name);
    wchar_t pattern[1100]; swprintf(pattern, 1100, L"%ls\\*", source);
    WIN32_FIND_DATAW found; HANDLE find = FindFirstFileW(pattern, &found);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if (!wcscmp(found.cFileName, L".") || !wcscmp(found.cFileName, L"..")) continue;
        wchar_t child[1100]; swprintf(child, 1100, L"%ls\\%ls", source, found.cFileName);
        add_tree(list, child, inner, depth + 1);
    } while (FindNextFileW(find, &found));
    FindClose(find);
}
int joblist_add_sources(JobList *list, const wchar_t *const *paths, int path_count, const char *dest_dir)
{
    for (int i = 0; i < path_count; i++) add_tree(list, paths[i], dest_dir, 0);
    return list->count;
}
static int compare_small_first(const void *a, const void *b)
{
    const CopyJob *x = a, *y = b;
    if (x->item.is_dir != y->item.is_dir) return y->item.is_dir - x->item.is_dir;      /* 폴더가 앞 */
    if (x->item.is_dir) return 0;
    return x->item.size < y->item.size ? -1 : x->item.size > y->item.size;
}
void joblist_sort_small_first(JobList *list)
{
    /* qsort 는 안정 정렬이 아니라서 폴더끼리 순서가 바뀔 수 있다. 폴더는 길이 짧은(위쪽) 것부터 만들어야 하므로 길이 순으로 다시 정렬 */
    qsort(list->jobs, (size_t)list->count, sizeof(CopyJob), compare_small_first);
    int dirs = 0; while (dirs < list->count && list->jobs[dirs].item.is_dir) dirs++;
    for (int i = 1; i < dirs; i++) {                     /* 삽입 정렬: 경로가 짧은 폴더가 앞 (부모가 자식보다 먼저) */
        CopyJob key = list->jobs[i]; int j = i - 1;
        size_t key_len = strlen(key.item.dest_dir) + strlen(key.item.name);
        while (j >= 0 && strlen(list->jobs[j].item.dest_dir) + strlen(list->jobs[j].item.name) > key_len) { list->jobs[j + 1] = list->jobs[j]; j--; }
        list->jobs[j + 1] = key;
    }
}
VpItem *joblist_items(const JobList *list)
{
    VpItem *items = malloc(sizeof(VpItem) * (size_t)(list->count ? list->count : 1));
    if (!items) return NULL;
    for (int i = 0; i < list->count; i++) items[i] = list->jobs[i].item;
    return items;
}

/* ---------- 복사 ---------- */
typedef struct { const JobCallbacks *cb; uint64_t base_done, total; const char *name; int stop; } CopyState;
static void note_error(JobReport *report, const char *what, const char *name, DWORD code)
{
    report->failed++;
    if (!report->first_error[0]) snprintf(report->first_error, sizeof report->first_error, "%s: %s (오류 %lu)", what, name, (unsigned long)code);
}
static DWORD CALLBACK copy_progress(LARGE_INTEGER total, LARGE_INTEGER done, LARGE_INTEGER s, LARGE_INTEGER sd, DWORD stream, DWORD reason, HANDLE src, HANDLE dst, LPVOID data)
{
    (void)total; (void)s; (void)sd; (void)stream; (void)reason; (void)src; (void)dst;
    CopyState *state = data;
    if (state->cb->progress) state->cb->progress(state->cb->user, state->base_done + (uint64_t)done.QuadPart, state->total, state->name);
    if (state->cb->cancelled && state->cb->cancelled(state->cb->user)) { state->stop = 1; return PROGRESS_CANCEL; }
    return PROGRESS_CONTINUE;
}
static void dest_wide_path(const VpItem *item, wchar_t *out, int cap)
{
    char full[VP_PATH + 260]; snprintf(full, sizeof full, "%s\\%s", item->dest_dir, item->name);
    text_to_wide(full, out, cap);
}
void run_copy(const JobList *list, int count, const JobCallbacks *cb, JobReport *report)
{
    memset(report, 0, sizeof *report);
    CopyState state = { cb, 0, 0, "", 0 };
    for (int i = 0; i < count; i++) if (!list->jobs[i].item.is_dir) state.total += list->jobs[i].item.size;
    for (int i = 0; i < count && !state.stop; i++) {
        const CopyJob *job = &list->jobs[i];
        wchar_t dest[VP_PATH + 300]; dest_wide_path(&job->item, dest, VP_PATH + 300);
        if (job->item.is_dir) {
            if (CreateDirectoryW(dest, NULL)) report->created_dirs++;
            else if (GetLastError() != ERROR_ALREADY_EXISTS) note_error(report, "폴더 만들기 실패", job->item.name, GetLastError());
            continue;
        }
        state.name = job->item.name;
        SetFileAttributesW(dest, FILE_ATTRIBUTE_NORMAL);                      /* 읽기 전용 파일을 덮어쓸 수 있게 */
        if (CopyFileExW(job->source, dest, copy_progress, &state, NULL, 0)) {
            report->copied_files++; report->copied_bytes += job->item.size; state.base_done += job->item.size;
            SetFileAttributesW(dest, FILE_ATTRIBUTE_NORMAL);                  /* 원본이 읽기 전용이어도 가상 하드에선 지울 수 있게 */
            if (cb->progress) cb->progress(cb->user, state.base_done, state.total, job->item.name);   /* 작은 파일은 중간 알림이 없을 수 있어 끝날 때도 알림 */
        } else if (!state.stop) note_error(report, "복사 실패", job->item.name, GetLastError());
    }
    report->cancelled = state.stop;
}

/* ---------- 삭제 ---------- */
static int remove_tree(const wchar_t *path, JobReport *report)
{
    DWORD attr = GetFileAttributesW(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return 0;
    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        if (DeleteFileW(path)) { report->copied_files++; return 0; }
        char name[256]; wide_to_text(base_name(path), name, sizeof name); note_error(report, "지우기 실패", name, GetLastError()); return -1;
    }
    wchar_t pattern[1100]; swprintf(pattern, 1100, L"%ls\\*", path);
    WIN32_FIND_DATAW found; HANDLE find = FindFirstFileW(pattern, &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(found.cFileName, L".") || !wcscmp(found.cFileName, L"..")) continue;
            wchar_t child[1100]; swprintf(child, 1100, L"%ls\\%ls", path, found.cFileName);
            remove_tree(child, report);
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    if (RemoveDirectoryW(path)) { report->created_dirs++; return 0; }
    char name[256]; wide_to_text(base_name(path), name, sizeof name); note_error(report, "폴더 지우기 실패", name, GetLastError());
    return -1;
}
void run_delete(const char *const *paths, int path_count, const char *root, JobReport *report)
{
    memset(report, 0, sizeof *report);
    size_t root_len = strlen(root);
    for (int i = 0; i < path_count; i++) {
        /* 가상 하드 폴더 안의 항목만 지운다 (뿌리 자체나 그 밖은 거절) */
        if (_strnicmp(paths[i], root, root_len) || paths[i][root_len] != '\\' || strstr(paths[i], "\\..")) { note_error(report, "범위 밖이라 거절", paths[i], 0); continue; }
        wchar_t wide[VP_PATH + 300]; text_to_wide(paths[i], wide, VP_PATH + 300);
        remove_tree(wide, report);
    }
}
