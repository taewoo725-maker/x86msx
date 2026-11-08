/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* winhost.c - X86VHD V1.0.0 Windows 바닥 일. 설명은 winhost.h */
#include "winhost.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void text_to_wide(const char *utf8, wchar_t *out, int cap) { if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap)) out[0] = 0; }
void wide_to_text(const wchar_t *wide, char *out, int cap) { if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, cap, NULL, NULL)) out[0] = 0; }

/* ---------- 폴더 목록 호스트 ---------- */
static int host_list(void *ctx, const char *path, VfHostEnt *out, int max)
{
    (void)ctx;
    wchar_t pattern[VP_PATH + 4];
    text_to_wide(path, pattern, VP_PATH);
    wcscat(pattern, L"\\*");
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW(pattern, &found);
    if (find == INVALID_HANDLE_VALUE) return -1;
    int count = 0;
    do {
        if (!wcscmp(found.cFileName, L".") || !wcscmp(found.cFileName, L"..")) continue;
        if (count >= max) break;
        VfHostEnt *e = &out[count++];
        memset(e, 0, sizeof *e);
        wide_to_text(found.cFileName, e->name, sizeof e->name);
        e->is_dir = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e->size = found.nFileSizeHigh ? 0xFFFFFFFFu : found.nFileSizeLow;       /* 4 GB 넘는 파일은 어차피 안 들어감 */
    } while (FindNextFileW(find, &found));
    FindClose(find);
    return count;
}
static int host_unused_read(void *c, const char *p, uint32_t o, uint8_t *b, uint32_t l) { (void)c; (void)p; (void)o; (void)b; (void)l; return -1; }
static int host_unused_write(void *c, const char *p, const uint8_t *d, uint32_t l) { (void)c; (void)p; (void)d; (void)l; return -1; }
static int host_unused_path(void *c, const char *p) { (void)c; (void)p; return -1; }
const VfHost *windows_host(void)
{
    static const VfHost host = { 0, host_list, host_unused_read, host_unused_write, host_unused_path, host_unused_path, 0 };
    return &host;
}

/* ---------- x86msx.ini ---------- */
static char *load_text_file(const char *path)
{
    wchar_t wide[VP_PATH]; text_to_wide(path, wide, VP_PATH);
    HANDLE f = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(f, NULL), got = 0;
    char *text = size < (16u << 20) ? malloc(size + 1) : NULL;
    if (text && ReadFile(f, text, size, &got, NULL)) text[got] = 0; else { free(text); text = NULL; }
    CloseHandle(f);
    return text;
}
static const char *find_key_value(const char *text, const char *key)       /* 줄 맨 앞 "key=" 의 값 시작. 없으면 NULL */
{
    size_t len = strlen(key);
    for (const char *line = text; line && *line; ) {
        if (!strncmp(line, key, len) && line[len] == '=') return line + len + 1;
        line = strchr(line, '\n'); if (line) line++;
    }
    return NULL;
}
static void copy_line(const char *value, char *out, int cap)
{
    int k = 0;
    while (value && *value && *value != '\r' && *value != '\n' && k < cap - 1) out[k++] = *value++;
    out[k] = 0;
}
int read_vhd_settings(const char *usb_root, VhdSlotSetting slots[VHD_SLOTS])
{
    char path[VP_PATH]; snprintf(path, sizeof path, "%sx86msx.ini", usb_root);
    char *text = load_text_file(path);
    if (!text) return -1;
    for (int i = 0; i < VHD_SLOTS; i++) {
        char key[16], line[VP_PATH];
        snprintf(key, sizeof key, "VhdDir%d", i + 1);
        copy_line(find_key_value(text, key), slots[i].folder, sizeof slots[i].folder);
        snprintf(key, sizeof key, "VhdSize%d", i + 1);
        copy_line(find_key_value(text, key), line, sizeof line);
        slots[i].size_mb = atoi(line); slots[i].kind = 0;
        const char *comma = strchr(line, ',');
        if (comma) slots[i].kind = atoi(comma + 1);
        if (slots[i].size_mb < 1 || slots[i].size_mb > 4095) slots[i].size_mb = 32;
        if (slots[i].kind < VF_AUTO || slots[i].kind > VF_FAT16) slots[i].kind = VF_AUTO;
    }
    free(text);
    return 0;
}

/* ---------- 가상 하드 폴더의 X86MSX.VHD (에뮬레이터가 "여기는 가상 하드"라고 표시해 둔 숨김 설정 파일) ---------- */
int read_vhd_folder_config(const char *folder_path, int *size_mb, int *kind)
{
    char path[VP_PATH]; snprintf(path, sizeof path, "%s\\X86MSX.VHD", folder_path);
    char *text = load_text_file(path);
    if (!text) return -1;                                   /* 파일이 없으면 가상 하드로 잡힌 폴더가 아님 */
    char line[64];
    *size_mb = 32; *kind = VF_AUTO;                         /* 에뮬레이터 vhd_cfg_read 와 같은 기본값 */
    const char *value = find_key_value(text, "Size");
    if (value) { copy_line(value, line, sizeof line); *size_mb = atoi(line); }
    value = find_key_value(text, "Format");
    if (value) { copy_line(value, line, sizeof line); *kind = atoi(line); }
    if (*size_mb < 1 || *size_mb > 4095) *size_mb = 32;
    if (*kind < VF_AUTO || *kind > VF_FAT16) *kind = VF_AUTO;
    free(text);
    return 0;
}


/* ---------- USB 찾기 ---------- */
void vmfolder_path(const char *usb_root, char *out, int cap) { snprintf(out, (size_t)cap, "%sStorage Device\\VMFolder", usb_root); }
static int path_exists(const char *path, int want_dir)
{
    wchar_t wide[VP_PATH]; text_to_wide(path, wide, VP_PATH);
    DWORD attr = GetFileAttributesW(wide);
    if (attr == INVALID_FILE_ATTRIBUTES) return 0;
    return want_dir ? (attr & FILE_ATTRIBUTE_DIRECTORY) != 0 : (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
int is_x86msx_root(const char *usb_root)                    /* x86msx.ini 나 Storage Device\VMFolder 가 있으면 X86MSX USB */
{
    char path[VP_PATH];
    snprintf(path, sizeof path, "%sx86msx.ini", usb_root);
    if (path_exists(path, 0)) return 1;
    vmfolder_path(usb_root, path, sizeof path);
    return path_exists(path, 1);
}
int find_usb_roots(char roots[][8], int max)
{
    int count = 0;
    DWORD mask = GetLogicalDrives();
    for (int letter = 0; letter < 26 && count < max; letter++) {
        if (!(mask & (1u << letter))) continue;
        char root[8]; snprintf(root, sizeof root, "%c:\\", 'A' + letter);
        if (is_x86msx_root(root)) snprintf(roots[count++], 8, "%s", root);
    }
    return count;
}
