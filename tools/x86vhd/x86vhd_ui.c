/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* x86vhd_ui.c - X86VHD V1.0.0 창. 판정·복사·삭제는 vhdplan.c / jobs.c 가 하고 여기서는 보여 주고 받기만 한다.
 *
 * 구성: 위 띠(제목) → 가상 하드 고르기 → 용량 카드(쓴 양·게이지·용량·형식) → 폴더 경로 → 파일 목록 → 버튼 → 상태 */
#include "x86vhd_version.h"
#include "jobs.h"
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

/* ---------- 색 ---------- */
#define COLOR_WINDOW_BG   RGB(0xF3, 0xF5, 0xF9)
#define COLOR_HEADER_BG   RGB(0x1F, 0x2A, 0x44)
#define COLOR_CARD_BG     RGB(0xFF, 0xFF, 0xFF)
#define COLOR_CARD_LINE   RGB(0xD8, 0xDE, 0xE9)
#define COLOR_TEXT        RGB(0x1E, 0x25, 0x33)
#define COLOR_MUTED       RGB(0x6B, 0x75, 0x89)
#define COLOR_ACCENT      RGB(0x2F, 0x6F, 0xEB)
#define COLOR_OK          RGB(0x2E, 0x9E, 0x6B)
#define COLOR_WARN        RGB(0xE0, 0xA0, 0x30)
#define COLOR_FOLDER_TEXT RGB(0x1F, 0x5F, 0xBF)   /* 파일 목록에서 폴더 줄 글자색 (파일은 COLOR_TEXT) */
#define COLOR_BAD         RGB(0xD6, 0x45, 0x45)
#define COLOR_TRACK       RGB(0xE6, 0xEA, 0xF2)

#define SIDE_MARGIN 34                                   /* 창 좌우 여백 (96dpi 픽셀) */

/* ---------- 컨트롤 번호 ---------- */
enum {
    ID_USB = 100, ID_DRIVE, ID_REFRESH, ID_SIZE, ID_KIND, ID_UP, ID_PATH, ID_LIST,
    ID_ADD_FILES, ID_ADD_FOLDER, ID_DELETE, ID_OPEN_EXPLORER, ID_STOP, ID_STATUS, ID_PROGRESS
};
enum { MAX_DRIVE_ITEMS = 96 };

/* 가상 하드 풀다운 한 줄이 가리키는 폴더 (VMFolder 아래). slot = 연결된 드라이브 0~5 (C:~H:) */
typedef struct { char folder[VP_PATH]; int slot, size_mb, kind; } DriveItem;
enum { CHOICE_FIRST = 201, CHOICE_SECOND = 202 };
enum { MAX_USB = 8, MAX_NAMES = 20000 };

static const wchar_t *const kind_labels[] = { L"자동", L"DOS1", L"FAT12", L"FAT16" };
static const int size_choices[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4095 };

/* ---------- 앱 상태 ---------- */
typedef struct {
    HWND main, usb_combo, drive_combo, size_combo, kind_combo, list, path_label, status, progress;
    HWND btn_refresh, btn_up, btn_add_files, btn_add_folder, btn_delete, btn_open, btn_stop;
    HFONT font, font_bold, font_big, font_small;
    int dpi;
    RECT card;
    POINT usb_label_at, drive_label_at;                        /* 위쪽 한 줄의 이름표 자리 (배치할 때 정함) */
    char usb_root[VP_PATH];
    char usb_roots[MAX_USB][VP_PATH]; int usb_count;           /* USB 풀다운 항목 (찾은 것 + 직접 고른 것) */
    VhdSlotSetting slots[VHD_SLOTS];
    DriveItem drive_items[MAX_DRIVE_ITEMS]; int drive_item_count;
    char vmfolder[VP_PATH];
    char vhd_root[VP_PATH], cur_dir[VP_PATH];
    int size_mb, kind;
    int folder_ready;
    VpTree tree;
    VpFit fit;
    int file_count;
    VpName *names; int name_count;
    int busy, cancel_requested;
    HANDLE change_watch; int outside_change_pending;          /* 탐색기 등 밖에서 폴더가 바뀌었는지 지켜보는 알림 */
    DWORD last_progress_tick;
} App;
static App app;

static int S(int px) { return MulDiv(px, app.dpi, 96); }

/* ---------- 작은 도우미 ---------- */
static void format_mb(wchar_t *out, int cap, uint32_t kb)       /* 1234 KB → "1.2 MB" */
{
    if (kb < 1024) swprintf(out, (size_t)cap, L"%u KB", kb);
    else if (kb < 10 * 1024) swprintf(out, (size_t)cap, L"%.2f MB", kb / 1024.0);
    else swprintf(out, (size_t)cap, L"%.1f MB", kb / 1024.0);
}
static void format_bytes(wchar_t *out, int cap, uint64_t bytes) { format_mb(out, cap, (uint32_t)((bytes + 1023) / 1024)); }
static void set_status(const wchar_t *text) { SetWindowTextW(app.status, text); }
static void set_status_fmt(const wchar_t *fmt, ...)
{
    wchar_t text[600]; va_list args; va_start(args, fmt); vswprintf(text, 600, fmt, args); va_end(args);
    set_status(text);
}
static void pump_messages(void)
{
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}
static void set_control_font(HWND control, HFONT font) { SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE); }
static HWND make_control(const wchar_t *cls, const wchar_t *text, DWORD style, int id)
{
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, app.main, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    set_control_font(h, app.font);
    return h;
}

/* ---------- 선택지 창 (TaskDialog, 없으면 MessageBox) ---------- */
typedef HRESULT (WINAPI *TaskDialogIndirectFn)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);
/* 선택지 labels[0..count-1] 중 고른 번호, 취소 -1 */
static int ask_choice(const wchar_t *instruction, const wchar_t *content, const wchar_t *const *labels, int count, const wchar_t *cancel_label)
{
    static TaskDialogIndirectFn task_dialog;
    if (!task_dialog) { HMODULE h = LoadLibraryW(L"comctl32.dll"); if (h) task_dialog = (TaskDialogIndirectFn)GetProcAddress(h, "TaskDialogIndirect"); }
    if (task_dialog && count <= 3) {
        TASKDIALOG_BUTTON buttons[3];
        for (int i = 0; i < count; i++) { buttons[i].nButtonID = CHOICE_FIRST + i; buttons[i].pszButtonText = labels[i]; }
        TASKDIALOGCONFIG cfg; memset(&cfg, 0, sizeof cfg);
        cfg.cbSize = sizeof cfg; cfg.hwndParent = app.main;
        cfg.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
        cfg.dwCommonButtons = cancel_label ? TDCBF_CANCEL_BUTTON : 0;
        cfg.pszWindowTitle = L"X86VHD"; cfg.pszMainInstruction = instruction; cfg.pszContent = content;
        cfg.pszMainIcon = TD_INFORMATION_ICON;
        cfg.cButtons = (UINT)count; cfg.pButtons = buttons;
        int pressed = 0;
        if (SUCCEEDED(task_dialog(&cfg, &pressed, NULL, NULL))) return pressed >= CHOICE_FIRST && pressed < CHOICE_FIRST + count ? pressed - CHOICE_FIRST : -1;
    }
    wchar_t text[1600]; swprintf(text, 1600, L"%ls\n\n%ls\n\n예 = %ls", instruction, content, labels[0]);
    int r = MessageBoxW(app.main, text, L"X86VHD", MB_YESNOCANCEL | MB_ICONINFORMATION);
    return r == IDYES ? 0 : (r == IDNO && count > 1 ? 1 : -1);
}
static void show_message(const wchar_t *instruction, const wchar_t *content)
{
    const wchar_t *ok = L"확인";
    ask_choice(instruction, content, &ok, 1, NULL);
}

/* ---------- 사진 새로 찍기 · 판정 · 목록 ---------- */
static int compare_names(const void *a, const void *b)
{
    const VpName *x = a, *y = b;
    if (x->is_dir != y->is_dir) return y->is_dir - x->is_dir;
    return _stricmp(x->host_name, y->host_name);
}
static void fill_list(void)
{
    ListView_DeleteAllItems(app.list);
    free(app.names); app.names = NULL; app.name_count = 0;
    if (!app.folder_ready) return;
    app.names = malloc(sizeof(VpName) * MAX_NAMES);
    if (!app.names) return;
    app.name_count = vp_names(&app.tree, (uint32_t)app.size_mb, app.kind, app.cur_dir, app.names, MAX_NAMES);
    qsort(app.names, (size_t)app.name_count, sizeof(VpName), compare_names);
    int row = 0;
    if (_stricmp(app.cur_dir, app.vhd_root)) {
        LVITEMW up; memset(&up, 0, sizeof up); up.mask = LVIF_TEXT; up.iItem = row; up.pszText = L"..  (위 폴더)";
        ListView_InsertItem(app.list, &up);
        ListView_SetItemText(app.list, row, 3, L"폴더");
        row++;
    }
    for (int i = 0; i < app.name_count; i++, row++) {
        wchar_t wide[300], msx[20], size[32];
        text_to_wide(app.names[i].host_name, wide, 300);
        text_to_wide(app.names[i].msx_name, msx, 20);
        LVITEMW item; memset(&item, 0, sizeof item); item.mask = LVIF_TEXT; item.iItem = row; item.pszText = wide;
        ListView_InsertItem(app.list, &item);
        ListView_SetItemText(app.list, row, 1, msx);
        if (app.names[i].is_dir) wcscpy(size, L""); else format_bytes(size, 32, app.names[i].size);
        ListView_SetItemText(app.list, row, 2, size);
        ListView_SetItemText(app.list, row, 3, app.names[i].is_dir ? L"폴더" : L"파일");
    }
    wchar_t path[VP_PATH]; text_to_wide(app.cur_dir, path, VP_PATH);
    SetWindowTextW(app.path_label, path);
    EnableWindow(app.btn_up, _stricmp(app.cur_dir, app.vhd_root) != 0);
}
static void recheck_capacity(void)
{
    memset(&app.fit, 0, sizeof app.fit);
    if (app.folder_ready) vp_check(&app.tree, (uint32_t)app.size_mb, app.kind, &app.fit);
    InvalidateRect(app.main, &app.card, FALSE);
}
static void count_files(void)
{
    app.file_count = 0;
    for (int i = 0; i < app.tree.count; i++) if (!app.tree.nodes[i].ent.is_dir) app.file_count++;
}
static void rescan_folder(void)
{
    vp_free(&app.tree);
    vp_init(&app.tree, app.vhd_root);
    app.folder_ready = app.vhd_root[0] && vp_scan(&app.tree, windows_host()) == 0;
    count_files();
    recheck_capacity();
    fill_list();
    if (!app.folder_ready) set_status(L"가상 하드 폴더를 읽지 못했습니다. USB 가 꽂혀 있는지 확인해 주세요.");
}
static void refresh_all(void) { rescan_folder(); }

/* ---------- 밖에서 바뀐 것 따라가기 (탐색기에서 파일을 넣고 빼고 이름을 바꿔도 X86VHD 에 반영) ---------- */
#define TIMER_WATCH_ID       1
#define TIMER_WATCH_MS       500                           /* 이 간격으로 확인. 조용해진 뒤 한 번만 다시 읽음 */
static void stop_watching_folder(void)
{
    if (app.change_watch) FindCloseChangeNotification(app.change_watch);
    app.change_watch = NULL; app.outside_change_pending = 0;
}
static void start_watching_folder(void)
{
    stop_watching_folder();
    if (!app.vhd_root[0]) return;
    wchar_t wide[VP_PATH]; text_to_wide(app.vhd_root, wide, VP_PATH);
    HANDLE watch = FindFirstChangeNotificationW(wide, TRUE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
    app.change_watch = watch == INVALID_HANDLE_VALUE ? NULL : watch;
}
static int folder_changed_since_last_check(void)           /* 알림이 왔으면 1 (알림을 다시 걸어 둠) */
{
    if (!app.change_watch || WaitForSingleObject(app.change_watch, 0) != WAIT_OBJECT_0) return 0;
    FindNextChangeNotification(app.change_watch);
    return 1;
}
static int directory_exists(const char *path)
{
    wchar_t wide[VP_PATH]; text_to_wide(path, wide, VP_PATH);
    DWORD attributes = GetFileAttributesW(wide);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* ---------- 드라이브 고르기 ---------- */
static void select_size_in_combo(int mb)
{
    int found = -1, count = (int)SendMessageW(app.size_combo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++) if ((int)SendMessageW(app.size_combo, CB_GETITEMDATA, i, 0) == mb) found = i;
    if (found < 0) {                                       /* 표준 값이 아니면 그 값을 목록에 더함 */
        wchar_t text[24]; swprintf(text, 24, L"%d MB", mb);
        found = (int)SendMessageW(app.size_combo, CB_ADDSTRING, 0, (LPARAM)text);
        SendMessageW(app.size_combo, CB_SETITEMDATA, found, mb);
    }
    SendMessageW(app.size_combo, CB_SETCURSEL, found, 0);
}
static void use_folder(const char *vhd_root, int size_mb, int kind)
{
    snprintf(app.vhd_root, sizeof app.vhd_root, "%s", vhd_root);
    snprintf(app.cur_dir, sizeof app.cur_dir, "%s", vhd_root);
    app.size_mb = size_mb; app.kind = kind;
    select_size_in_combo(size_mb);
    SendMessageW(app.kind_combo, CB_SETCURSEL, kind, 0);
    start_watching_folder();
    refresh_all();
}
static void select_drive_item(int item)
{
    if (item < 0 || item >= app.drive_item_count) return;
    const DriveItem *it = &app.drive_items[item];
    char path[VP_PATH]; snprintf(path, sizeof path, "%s\\%s", app.vmfolder, it->folder);
    use_folder(path, it->size_mb, it->kind);
}

/* ---- 가상 하드 찾기: 연결된 것(x86msx.ini VhdDir1~6) + 연결 안 됐어도 폴더에 X86MSX.VHD 가 있는 것 ---- */
static int folder_is_listed(const char *folder)
{
    for (int i = 0; i < app.drive_item_count; i++) if (!_stricmp(app.drive_items[i].folder, folder)) return 1;
    return 0;
}
static void add_connected_items(void)
{
    for (int slot = 0; slot < VHD_SLOTS; slot++) {
        if (!app.slots[slot].folder[0] || folder_is_listed(app.slots[slot].folder)) continue;
        DriveItem *it = &app.drive_items[app.drive_item_count++];
        snprintf(it->folder, sizeof it->folder, "%s", app.slots[slot].folder);
        it->slot = slot; it->size_mb = app.slots[slot].size_mb; it->kind = app.slots[slot].kind;
    }
}
static void add_recognized_unconnected_items(void)       /* VMFolder 바로 아래 폴더 중 X86MSX.VHD 가 있는 것만 */
{
    static VfHostEnt entries[256];
    int count = windows_host()->list(NULL, app.vmfolder, entries, 256);
    for (int i = 0; i < count && app.drive_item_count < MAX_DRIVE_ITEMS; i++) {
        if (!entries[i].is_dir || folder_is_listed(entries[i].name)) continue;
        char path[VP_PATH]; snprintf(path, sizeof path, "%s\\%s", app.vmfolder, entries[i].name);
        int size_mb, kind;
        if (read_vhd_folder_config(path, &size_mb, &kind) != 0) continue;       /* 가상 하드로 안 잡힌 폴더는 뺀다 */
        DriveItem *it = &app.drive_items[app.drive_item_count++];
        snprintf(it->folder, sizeof it->folder, "%s", entries[i].name);
        it->slot = -1; it->size_mb = size_mb; it->kind = kind;
    }
}
static void find_drive_items(void)
{
    app.drive_item_count = 0;
    add_connected_items();
    add_recognized_unconnected_items();
}
/* 풀다운 한 줄(드라이브 글자 없이): "NEXTORA   2.10 MB / 3.95 MB 사용 · FAT16", 용량을 넘으면 끝에 [넘침] */
static void describe_item(const DriveItem *it, wchar_t *out, int cap)
{
    wchar_t name[200], used[32], total[32];
    text_to_wide(it->folder, name, 200);
    char path[VP_PATH]; snprintf(path, sizeof path, "%s\\%s", app.vmfolder, it->folder);
    VpTree tree; vp_init(&tree, path);
    if (vp_scan(&tree, windows_host()) != 0) { swprintf(out, (size_t)cap, L"%ls      (폴더를 읽지 못했습니다)", name); vp_free(&tree); return; }
    VpFit fit; vp_check(&tree, (uint32_t)it->size_mb, it->kind, &fit);
    format_mb(used, 32, fit.used_kb); format_mb(total, 32, fit.cap_kb);
    swprintf(out, (size_t)cap, L"%ls      %ls / %ls 사용  ·  %ls%ls", name, used, total,
             kind_labels[fit.kind >= VF_DOS1 && fit.kind <= VF_FAT16 ? fit.kind : 0], fit.fits ? L"" : L"  ·  [넘침]");
    vp_free(&tree);
}
static int build_drive_items(void)                              /* 풀다운 항목을 다시 찾아 채우고 가상 하드 수를 돌려줌 */
{
    find_drive_items();
    SendMessageW(app.drive_combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < app.drive_item_count; i++) {
        wchar_t text[400]; describe_item(&app.drive_items[i], text, 400);
        SendMessageW(app.drive_combo, CB_ADDSTRING, 0, (LPARAM)text);
    }
    SendMessageW(app.drive_combo, CB_SETITEMHEIGHT, 0, S(28));               /* 펼친 목록 줄 높이 */
    return app.drive_item_count;
}
static void refresh_drive_labels(void)                          /* 고른 항목은 그대로 두고 숫자만 새로 읽음 */
{
    if (!app.vmfolder[0]) return;
    int selected = (int)SendMessageW(app.drive_combo, CB_GETCURSEL, 0, 0);
    build_drive_items();
    SendMessageW(app.drive_combo, CB_SETCURSEL, selected, 0);
}
static void follow_outside_change(void)                         /* 밖에서 바뀐 뒤: 다시 읽고 숫자도 새로 */
{
    if (!app.vhd_root[0]) return;
    if (!directory_exists(app.cur_dir)) snprintf(app.cur_dir, sizeof app.cur_dir, "%s", app.vhd_root);   /* 보던 폴더가 지워졌으면 맨 위로 */
    refresh_all();
    refresh_drive_labels();
}
static void on_watch_timer(void)
{
    int changed = folder_changed_since_last_check();
    if (app.busy) { app.outside_change_pending = 0; return; }   /* 복사·삭제 중 자기가 만든 변경은 끝나고 어차피 다시 읽음 */
    if (changed) { app.outside_change_pending = 1; return; }    /* 아직 바뀌는 중 → 다음 확인까지 기다림 */
    if (app.outside_change_pending) { app.outside_change_pending = 0; follow_outside_change(); }
}
static void fill_drive_combo(void)
{
    int items = build_drive_items();
    if (items > 0) { SendMessageW(app.drive_combo, CB_SETCURSEL, 0, 0); select_drive_item(0); }
    else { SendMessageW(app.drive_combo, CB_SETCURSEL, -1, 0); app.folder_ready = 0; app.vhd_root[0] = 0; vp_free(&app.tree); fill_list(); recheck_capacity(); }
}
static void remember_usb_root(void)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\X86VHD", 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) != ERROR_SUCCESS) return;
    wchar_t wide[VP_PATH]; text_to_wide(app.usb_root, wide, VP_PATH);
    RegSetValueExW(key, L"LastUsbRoot", 0, REG_SZ, (const BYTE *)wide, (DWORD)((wcslen(wide) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}
static void recall_usb_root(char *out, int cap)
{
    out[0] = 0; HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\X86VHD", 0, KEY_READ, &key) != ERROR_SUCCESS) return;
    wchar_t wide[VP_PATH] = L""; DWORD size = sizeof wide, type = 0;
    if (RegQueryValueExW(key, L"LastUsbRoot", NULL, &type, (BYTE *)wide, &size) == ERROR_SUCCESS && type == REG_SZ) wide_to_text(wide, out, cap);
    RegCloseKey(key);
}
static void use_usb_root(const char *root)
{
    snprintf(app.usb_root, sizeof app.usb_root, "%s", root);
    vmfolder_path(root, app.vmfolder, sizeof app.vmfolder);
    memset(app.slots, 0, sizeof app.slots);
    if (read_vhd_settings(root, app.slots) != 0) {
        for (int i = 0; i < VHD_SLOTS; i++) { app.slots[i].size_mb = 32; app.slots[i].kind = VF_AUTO; }
        set_status(L"x86msx.ini 를 읽지 못했습니다. 아래 목록에서 폴더를 직접 고르세요.");
    }
    remember_usb_root();
    fill_drive_combo();
}
static int usb_index_of(const char *root)
{
    for (int i = 0; i < app.usb_count; i++) if (!_stricmp(app.usb_roots[i], root)) return i;
    return -1;
}
static int add_usb_root(const char *root)                      /* 이미 있으면 그 번호, 없으면 더함 */
{
    int at = usb_index_of(root);
    if (at >= 0 || app.usb_count >= MAX_USB) return at;
    snprintf(app.usb_roots[app.usb_count], VP_PATH, "%s", root);
    return app.usb_count++;
}
static void usb_item_text(const char *root, wchar_t *out, int cap)         /* "X86MSX  E:" 처럼 X86MSX 와 드라이브 글자만 */
{
    swprintf(out, (size_t)cap, L"X86MSX  %lc:", (wchar_t)root[0]);
}
static void fill_usb_combo(int select_index)
{
    SendMessageW(app.usb_combo, CB_RESETCONTENT, 0, 0);
    for (int i = 0; i < app.usb_count; i++) { wchar_t text[200]; usb_item_text(app.usb_roots[i], text, 200); SendMessageW(app.usb_combo, CB_ADDSTRING, 0, (LPARAM)text); }
    SendMessageW(app.usb_combo, CB_SETITEMHEIGHT, 0, S(28));
    SendMessageW(app.usb_combo, CB_SETCURSEL, select_index, 0);
}
static void detect_usb_roots(void)                             /* 지금 꽂힌 USB 를 목록에 더함 (직접 고른 것은 남김) */
{
    char roots[MAX_USB][8]; int found = find_usb_roots(roots, MAX_USB);
    for (int i = 0; i < found; i++) add_usb_root(roots[i]);
}
static void refresh_usb_list(void)                              /* 풀다운을 열 때마다: 새로 꽂은 USB 가 보이게. 고른 USB 는 그대로 */
{
    detect_usb_roots();
    fill_usb_combo(usb_index_of(app.usb_root));
}
static void find_usb_automatically(void)
{
    detect_usb_roots();
    if (!app.usb_count) { fill_usb_combo(-1); set_status(L"X86MSX 가상 하드 폴더(Storage Device\\VMFolder)가 있는 USB 를 찾지 못했습니다. X86MSX 를 넣은 USB 를 꽂고 [새로 고침] 을 눌러 주세요."); fill_drive_combo(); return; }
    char last[VP_PATH]; recall_usb_root(last, sizeof last);
    int pick = usb_index_of(last); if (pick < 0) pick = 0;
    fill_usb_combo(pick);
    use_usb_root(app.usb_roots[pick]);
}
static void on_usb_selected(void)
{
    int item = (int)SendMessageW(app.usb_combo, CB_GETCURSEL, 0, 0);
    if (item >= 0 && item < app.usb_count) use_usb_root(app.usb_roots[item]);
}

/* 명령줄에 USB 뿌리(또는 Storage Device\\VMFolder 가 든 폴더)를 주면 그것을 씀: X86VHD.exe E:\\ */
static int use_command_line_root(const wchar_t *cmdline)
{
    while (*cmdline == L' ') cmdline++;
    if (!*cmdline) return 0;
    wchar_t clean[VP_PATH]; int k = 0;
    for (; *cmdline && k < VP_PATH - 2; cmdline++) if (*cmdline != L'"') clean[k++] = *cmdline;
    while (k > 0 && clean[k - 1] == L' ') k--;
    if (k == 0) return 0;
    if (clean[k - 1] != L'\\') clean[k++] = L'\\';
    clean[k] = 0;
    char root[VP_PATH]; wide_to_text(clean, root, sizeof root);
    if (!is_x86msx_root(root)) { set_status(L"명령줄로 준 폴더에 X86MSX(x86msx.ini 또는 Storage Device\\VMFolder)가 없어 무시합니다."); return 0; }
    int at = add_usb_root(root);
    fill_usb_combo(at);
    use_usb_root(root);
    return 1;
}
/* ---------- 그리기 ---------- */
static void draw_text(HDC dc, HFONT font, COLORREF color, int x, int y, const wchar_t *text, UINT align)
{
    SelectObject(dc, font); SetTextColor(dc, color); SetBkMode(dc, TRANSPARENT); SetTextAlign(dc, align);
    TextOutW(dc, x, y, text, (int)wcslen(text));
}
static void fill_round_rect(HDC dc, const RECT *r, int radius, COLORREF fill, COLORREF line)
{
    HBRUSH brush = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, line);
    HGDIOBJ old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, pen);
    RoundRect(dc, r->left, r->top, r->right, r->bottom, radius, radius);
    SelectObject(dc, old_brush); SelectObject(dc, old_pen); DeleteObject(brush); DeleteObject(pen);
}
static COLORREF gauge_color(int percent) { return percent >= 95 ? COLOR_BAD : percent >= 80 ? COLOR_WARN : COLOR_OK; }
static void draw_chip(HDC dc, int *x, int y, const wchar_t *text, COLORREF color)
{
    SelectObject(dc, app.font_small);
    SIZE size; GetTextExtentPoint32W(dc, text, (int)wcslen(text), &size);
    RECT r = { *x, y, *x + size.cx + S(24), y + S(28) };
    fill_round_rect(dc, &r, S(24), RGB(0xF0, 0xF3, 0xFA), COLOR_CARD_LINE);
    draw_text(dc, app.font_small, color, r.left + S(12), y + S(5), text, TA_LEFT | TA_TOP);
    *x = r.right + S(10);
}
static void draw_capacity_card(HDC dc)
{
    const RECT *c = &app.card;
    fill_round_rect(dc, c, S(14), COLOR_CARD_BG, COLOR_CARD_LINE);
    int left = c->left + S(28), top = c->top + S(20);
    int right_column = c->right - S(300);                  /* 용량·형식 콤보가 서는 자리 */
    if (!app.folder_ready) {
        draw_text(dc, app.font_bold, COLOR_MUTED, left, top + S(10), L"가상 하드를 고르면 사용량이 여기에 나옵니다", TA_LEFT | TA_TOP);
        draw_text(dc, app.font_small, COLOR_MUTED, left, top + S(44), L"X86MSX 를 넣은 USB 를 꽂고 위의 USB 풀다운에서 고르세요.", TA_LEFT | TA_TOP);
    } else {
        wchar_t used[40], cap[40], line[160], left_text[40];
        format_mb(used, 40, app.fit.used_kb); format_mb(cap, 40, app.fit.cap_kb);
        uint32_t free_kb = app.fit.cap_kb > app.fit.used_kb ? app.fit.cap_kb - app.fit.used_kb : 0;
        int percent = app.fit.cap_kb ? (int)((uint64_t)app.fit.used_kb * 100 / app.fit.cap_kb) : 0;
        if (percent > 100) percent = 100;
        COLORREF accent = app.fit.fits ? gauge_color(percent) : COLOR_BAD;
        swprintf(line, 160, L"%ls", used);
        draw_text(dc, app.font_big, COLOR_TEXT, left, top, line, TA_LEFT | TA_TOP);
        SIZE size; SelectObject(dc, app.font_big); GetTextExtentPoint32W(dc, line, (int)wcslen(line), &size);
        swprintf(line, 160, L"/ %ls", cap);
        draw_text(dc, app.font, COLOR_MUTED, left + size.cx + S(10), top + S(14), line, TA_LEFT | TA_TOP);
        swprintf(line, 160, app.fit.fits ? L"%d %%" : L"넘침", percent);
        draw_text(dc, app.font_bold, accent, right_column - S(28), top + S(10), line, TA_RIGHT | TA_TOP);
        RECT track = { left, top + S(54), right_column - S(28), top + S(54) + S(16) };
        fill_round_rect(dc, &track, S(14), COLOR_TRACK, COLOR_TRACK);
        int width = (track.right - track.left) * (app.fit.fits ? percent : 100) / 100;
        if (width > S(14)) { RECT fill = { track.left, track.top, track.left + width, track.bottom }; fill_round_rect(dc, &fill, S(14), accent, accent); }
        else if (percent > 0 || !app.fit.fits) { RECT fill = { track.left, track.top, track.left + S(14), track.bottom }; fill_round_rect(dc, &fill, S(14), accent, accent); }
        int x = left, y = top + S(88);
        format_mb(left_text, 40, free_kb);
        swprintf(line, 160, L"남음 %ls", left_text); draw_chip(dc, &x, y, line, app.fit.fits ? COLOR_TEXT : COLOR_BAD);
        swprintf(line, 160, L"형식 %ls", kind_labels[app.fit.kind >= VF_DOS1 && app.fit.kind <= VF_FAT16 ? app.fit.kind : 0]);
        draw_chip(dc, &x, y, line, COLOR_TEXT);
        swprintf(line, 160, L"파일 %d개", app.file_count); draw_chip(dc, &x, y, line, COLOR_TEXT);
        if (app.fit.cap_mb != (uint32_t)app.size_mb) { swprintf(line, 160, L"형식 한도로 %u MB", app.fit.cap_mb); draw_chip(dc, &x, y, line, COLOR_WARN); }
        if (app.fit.truncated) draw_chip(dc, &x, y, L"파일·폴더 수 한도 초과", COLOR_BAD);
        if (!app.fit.fits) {
            wchar_t warn[300];
            if (!strcmp(app.fit.why, "full"))
                swprintf(warn, 300, L"폴더 내용(약 %u MB)이 용량보다 커서 에뮬레이터에서 연결되지 않습니다.%ls", app.fit.need_mb, app.fit.suggest_mb ? L"" : L" 이 형식으로는 담을 수 없습니다.");
            else if (!strcmp(app.fit.why, "root"))
                swprintf(warn, 300, L"맨 위 폴더에 든 항목이 이 형식의 한도를 넘었습니다. 폴더로 묶어 주세요.");
            else swprintf(warn, 300, L"이 설정으로는 연결되지 않습니다 (%hs).", app.fit.why);
            draw_text(dc, app.font_small, COLOR_BAD, left, y + S(40), warn, TA_LEFT | TA_TOP);
            if (app.fit.suggest_mb) {
                swprintf(warn, 300, L"권장: %u MB 이상으로 에뮬레이터 설정을 올리세요.", app.fit.suggest_mb);
                draw_text(dc, app.font_small, COLOR_MUTED, left, y + S(60), warn, TA_LEFT | TA_TOP);
            }
        }
    }
    /* 오른쪽 열: 용량·형식 이름표 (콤보는 자식 창) */
    draw_text(dc, app.font_small, COLOR_MUTED, right_column, c->top + S(18), L"용량 (에뮬레이터 설정과 같게)", TA_LEFT | TA_TOP);
    draw_text(dc, app.font_small, COLOR_MUTED, right_column, c->top + S(90), L"형식", TA_LEFT | TA_TOP);
}
static void paint_window(HDC dc, int width, int height)
{
    RECT all = { 0, 0, width, height };
    HBRUSH bg = CreateSolidBrush(COLOR_WINDOW_BG); FillRect(dc, &all, bg); DeleteObject(bg);
    RECT head = { 0, 0, width, S(72) };
    HBRUSH hb = CreateSolidBrush(COLOR_HEADER_BG); FillRect(dc, &head, hb); DeleteObject(hb);
    draw_text(dc, app.font_big, RGB(255, 255, 255), S(SIDE_MARGIN), S(12), L"X86VHD", TA_LEFT | TA_TOP);
    draw_text(dc, app.font_small, RGB(0xB8, 0xC4, 0xE0), S(SIDE_MARGIN), S(46), L"X86MSX 가상 하드 폴더에 용량에 맞게 복사·삭제", TA_LEFT | TA_TOP);
    wchar_t version[32]; swprintf(version, 32, L"V%hs", X86VHD_VERSION);
    draw_text(dc, app.font_small, RGB(0xB8, 0xC4, 0xE0), width - S(SIDE_MARGIN), S(28), version, TA_RIGHT | TA_TOP);
    draw_text(dc, app.font_bold, COLOR_TEXT, app.usb_label_at.x, app.usb_label_at.y, L"USB", TA_LEFT | TA_TOP);
    draw_text(dc, app.font_bold, COLOR_TEXT, app.drive_label_at.x, app.drive_label_at.y, L"가상 하드", TA_LEFT | TA_TOP);
    draw_capacity_card(dc);
}

/* ---------- 배치 ---------- */
static void layout_controls(int width, int height)
{
    int margin = S(SIDE_MARGIN), row_h = S(36), refresh_w = S(120), gap = S(14);
    int row_y = S(94), usb_label_w = S(52), drive_label_w = S(96);
    int usable = width - margin * 2 - refresh_w - gap * 3 - usb_label_w - drive_label_w;
    int usb_w = usable * 38 / 100, drive_w = usable - usb_w;           /* USB 풀다운 옆에 가상 하드 풀다운 */
    int x = margin;
    app.usb_label_at.x = x; app.usb_label_at.y = row_y + S(7); x += usb_label_w;
    MoveWindow(app.usb_combo, x, row_y, usb_w, S(300), TRUE); x += usb_w + gap;
    app.drive_label_at.x = x; app.drive_label_at.y = row_y + S(7); x += drive_label_w;
    MoveWindow(app.drive_combo, x, row_y, drive_w, S(300), TRUE);
    MoveWindow(app.btn_refresh, width - margin - refresh_w, row_y, refresh_w, row_h, TRUE);
    app.card.left = margin; app.card.right = width - margin; app.card.top = S(158); app.card.bottom = S(158) + S(196);
    int combo_x = app.card.right - S(300) + 0, combo_w = S(272);
    MoveWindow(app.size_combo, combo_x, app.card.top + S(42), combo_w, S(300), TRUE);
    MoveWindow(app.kind_combo, combo_x, app.card.top + S(114), combo_w, S(300), TRUE);
    int path_y = app.card.bottom + S(18);
    MoveWindow(app.btn_up, margin, path_y, S(120), S(32), TRUE);
    MoveWindow(app.path_label, margin + S(132), path_y + S(7), width - margin * 2 - S(132), S(22), TRUE);
    int list_top = path_y + S(46), actions_y = height - S(104);
    MoveWindow(app.list, margin, list_top, width - margin * 2, actions_y - list_top - S(10), TRUE);
    int bw = S(150), bh = S(40);
    x = margin;
    HWND buttons[] = { app.btn_add_files, app.btn_add_folder, app.btn_delete, app.btn_open };
    for (int i = 0; i < 4; i++) { MoveWindow(buttons[i], x, actions_y, bw, bh, TRUE); x += bw + S(12); }
    MoveWindow(app.btn_stop, width - margin - S(110), actions_y, S(110), bh, TRUE);
    MoveWindow(app.status, margin, height - S(52), width - margin * 2, S(22), TRUE);
    MoveWindow(app.progress, margin, height - S(26), width - margin * 2, S(12), TRUE);
    int col_width = (width - margin * 2 - GetSystemMetrics(SM_CXVSCROLL) - S(4));
    ListView_SetColumnWidth(app.list, 0, col_width * 46 / 100);
    ListView_SetColumnWidth(app.list, 1, col_width * 22 / 100);
    ListView_SetColumnWidth(app.list, 2, col_width * 18 / 100);
    ListView_SetColumnWidth(app.list, 3, col_width * 14 / 100);
}

/* ---------- 바쁜 동안 ---------- */
static void set_busy(int busy)
{
    app.busy = busy; app.cancel_requested = 0;
    HWND locked[] = { app.usb_combo, app.drive_combo, app.size_combo, app.kind_combo, app.btn_refresh, app.btn_up, app.btn_add_files, app.btn_add_folder, app.btn_delete, app.btn_open, app.list };
    for (unsigned i = 0; i < sizeof locked / sizeof locked[0]; i++) EnableWindow(locked[i], !busy);
    ShowWindow(app.btn_stop, busy ? SW_SHOW : SW_HIDE);
    ShowWindow(app.progress, busy ? SW_SHOW : SW_HIDE);
    SendMessageW(app.progress, PBM_SETPOS, 0, 0);
    if (!busy) EnableWindow(app.btn_up, _stricmp(app.cur_dir, app.vhd_root) != 0);
}
static void on_copy_progress(void *user, uint64_t done, uint64_t total, const char *name)
{
    (void)user;
    DWORD now = GetTickCount();
    if (now - app.last_progress_tick < 60) return;
    app.last_progress_tick = now;
    wchar_t wide[300], size_done[32], size_total[32];
    text_to_wide(name, wide, 300); format_bytes(size_done, 32, done); format_bytes(size_total, 32, total);
    set_status_fmt(L"복사 중…  %ls   (%ls / %ls)", wide, size_done, size_total);
    SendMessageW(app.progress, PBM_SETPOS, total ? (WPARAM)(done * 1000 / total) : 0, 0);
    pump_messages();
}
static int on_copy_cancelled(void *user) { (void)user; pump_messages(); return app.cancel_requested; }

/* ---------- 복사 ---------- */
static int plan_count(JobList *jobs, VpFit *all)
{
    VpItem *items = joblist_items(jobs);
    if (!items) return 0;
    int k = vp_plan_fit(&app.tree, items, jobs->count, (uint32_t)app.size_mb, app.kind, all);
    free(items);
    return k;
}
static int count_files_in(const JobList *jobs, int upto)
{
    int files = 0;
    for (int i = 0; i < upto; i++) if (!jobs->jobs[i].item.is_dir) files++;
    return files;
}
static void report_copy_result(const JobReport *report)
{
    wchar_t size[32]; format_bytes(size, 32, report->copied_bytes);
    wchar_t summary[400];
    swprintf(summary, 400, report->cancelled ? L"중지했습니다. 파일 %d개(%ls)를 복사했습니다." : L"복사 완료: 파일 %d개(%ls)", report->copied_files, size);
    set_status(summary);
    if (report->failed) {
        wchar_t detail[400]; text_to_wide(report->first_error, detail, 400);
        wchar_t head[100]; swprintf(head, 100, L"%d건을 복사하지 못했습니다", report->failed);
        show_message(head, detail);
    }
}
static void copy_sources(const wchar_t *const *paths, int count)
{
    if (app.busy) return;
    if (!app.folder_ready) { show_message(L"먼저 가상 하드를 고르세요", L"위의 목록에서 C: ~ H: 중 하나를 고르거나 폴더를 직접 고르세요."); return; }
    set_status(L"들어갈 수 있는지 계산하는 중…"); pump_messages();
    JobList jobs; memset(&jobs, 0, sizeof jobs);
    joblist_add_sources(&jobs, paths, count, app.cur_dir);
    if (!jobs.count) { set_status(L"복사할 것이 없습니다."); joblist_free(&jobs); return; }
    VpFit all; int in_order = plan_count(&jobs, &all);
    int copy_count = in_order;
    if (in_order < jobs.count) {
        JobList small_first; memset(&small_first, 0, sizeof small_first);
        joblist_add_sources(&small_first, paths, count, app.cur_dir);
        joblist_sort_small_first(&small_first);
        VpFit unused; int by_size = plan_count(&small_first, &unused);
        int total_files = count_files_in(&jobs, jobs.count);
        wchar_t need[32], total[32], free_text[32], content[900], first_label[200], second_label[200];
        format_bytes(total, 32, jobs.file_bytes); format_mb(need, 32, all.need_mb * 1024); format_mb(free_text, 32, app.fit.cap_kb > app.fit.used_kb ? app.fit.cap_kb - app.fit.used_kb : 0);
        if (!strcmp(all.why, "root"))
            swprintf(content, 900, L"고른 것을 모두 넣으면 맨 위 폴더의 항목 수가 이 형식의 한도를 넘습니다.\n폴더로 묶어서 넣거나, 일부만 넣어 주세요.");
        else
            swprintf(content, 900, L"고른 파일 %d개(%ls)를 모두 넣으려면 하드가 약 %ls 필요한데, 지금은 %ls 남았습니다.\n%ls", total_files, total, need, free_text,
                     all.suggest_mb ? L"에뮬레이터 설정 용량을 올리면 모두 넣을 수 있습니다." : L"이 형식에는 다 담을 수 없습니다.");
        if (all.suggest_mb && strcmp(all.why, "root")) { wchar_t more[120]; swprintf(more, 120, L"\n권장: %u MB 이상", all.suggest_mb); wcscat(content, more); }
        swprintf(first_label, 200, L"고른 순서대로 들어가는 만큼만 복사\n파일 %d개", count_files_in(&jobs, in_order));
        swprintf(second_label, 200, L"작은 파일부터 채워서 복사\n파일 %d개", count_files_in(&small_first, by_size));
        const wchar_t *labels[2] = { first_label, second_label };
        int choice = ask_choice(L"용량이 모자랍니다", content, labels, 2, L"취소");
        if (choice < 0) { set_status(L"복사를 취소했습니다."); joblist_free(&jobs); joblist_free(&small_first); return; }
        if (choice == 1) { joblist_free(&jobs); jobs = small_first; copy_count = by_size; }
        else { joblist_free(&small_first); copy_count = in_order; }
    }
    if (count_files_in(&jobs, copy_count) == 0 && copy_count < jobs.count) { show_message(L"하나도 들어가지 않습니다", L"남은 용량이 너무 작습니다. 먼저 파일을 지우거나 용량 설정을 올려 주세요."); joblist_free(&jobs); return; }
    set_busy(1);
    JobCallbacks callbacks = { NULL, on_copy_progress, on_copy_cancelled };
    JobReport report; run_copy(&jobs, copy_count, &callbacks, &report);
    set_busy(0);
    joblist_free(&jobs);
    refresh_all();
    refresh_drive_labels();
    report_copy_result(&report);
    if (!app.fit.fits) show_message(L"복사했지만 설정 용량을 넘었습니다", L"폴더 내용이 용량보다 큽니다. 위 카드의 권장 용량으로 에뮬레이터 설정을 올리거나 파일을 지워 주세요.");
}
static void add_files_by_dialog(void)
{
    wchar_t *buffer = calloc(65536, sizeof(wchar_t));
    if (!buffer) return;
    OPENFILENAMEW ofn; memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn; ofn.hwndOwner = app.main; ofn.lpstrFile = buffer; ofn.nMaxFile = 65536;
    ofn.lpstrTitle = L"가상 하드에 넣을 파일 (여러 개 선택 가능)"; ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        wchar_t *paths[2048]; wchar_t (*store)[1024] = malloc(sizeof(wchar_t[1024]) * 2048);
        int count = 0;
        if (store) {
            const wchar_t *dir = buffer, *name = buffer + wcslen(buffer) + 1;
            if (!*name) { wcsncpy(store[0], dir, 1023); paths[count] = store[0]; count++; }      /* 파일 하나 */
            else for (; *name && count < 2048; name += wcslen(name) + 1) { swprintf(store[count], 1024, L"%ls\\%ls", dir, name); paths[count] = store[count]; count++; }
            copy_sources((const wchar_t *const *)paths, count);
            free(store);
        }
    }
    free(buffer);
}
static void add_folder_by_dialog(void)
{
    BROWSEINFOW bi; memset(&bi, 0, sizeof bi); wchar_t shown[MAX_PATH];
    bi.hwndOwner = app.main; bi.pszDisplayName = shown; bi.lpszTitle = L"가상 하드에 넣을 폴더를 고르세요 (안의 파일도 모두 넣습니다)"; bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST picked = SHBrowseForFolderW(&bi);
    if (!picked) return;
    wchar_t path[MAX_PATH]; BOOL ok = SHGetPathFromIDListW(picked, path); CoTaskMemFree(picked);
    if (ok) { const wchar_t *one = path; copy_sources(&one, 1); }
}
static void on_drop_files(HDROP drop)
{
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    wchar_t (*store)[1024] = malloc(sizeof(wchar_t[1024]) * (count ? count : 1));
    const wchar_t **paths = malloc(sizeof(wchar_t *) * (count ? count : 1));
    if (store && paths) {
        for (UINT i = 0; i < count; i++) { DragQueryFileW(drop, i, store[i], 1024); paths[i] = store[i]; }
        DragFinish(drop);
        copy_sources(paths, (int)count);
    } else DragFinish(drop);
    free(store); free(paths);
}

/* ---------- 삭제 ---------- */
static int selected_rows(int *rows, int max)
{
    int count = 0, row = -1;
    while (count < max && (row = ListView_GetNextItem(app.list, row, LVNI_SELECTED)) >= 0) rows[count++] = row;
    return count;
}
static const VpName *name_for_row(int row)
{
    int offset = _stricmp(app.cur_dir, app.vhd_root) ? 1 : 0;
    int index = row - offset;
    return index >= 0 && index < app.name_count ? &app.names[index] : NULL;
}
static void go_up(void)
{
    if (!_stricmp(app.cur_dir, app.vhd_root)) return;
    char *slash = strrchr(app.cur_dir, '\\');
    if (slash) *slash = 0;
    if (strlen(app.cur_dir) < strlen(app.vhd_root)) snprintf(app.cur_dir, sizeof app.cur_dir, "%s", app.vhd_root);
    fill_list();
}
static void enter_row(int row)
{
    int offset = _stricmp(app.cur_dir, app.vhd_root) ? 1 : 0;
    if (offset && row == 0) { go_up(); return; }
    const VpName *name = name_for_row(row);
    if (!name || !name->is_dir) return;
    size_t used = strlen(app.cur_dir);
    snprintf(app.cur_dir + used, sizeof app.cur_dir - used, "\\%s", name->host_name);
    fill_list();
}
static void delete_selected(void)
{
    if (app.busy || !app.folder_ready) return;
    int rows[4096]; int row_count = selected_rows(rows, 4096);
    char **paths = calloc((size_t)row_count + 1, sizeof(char *));
    int path_count = 0, dirs = 0, files = 0;
    VpTree after; vp_clone(&app.tree, &after);
    for (int i = 0; i < row_count && paths; i++) {
        const VpName *name = name_for_row(rows[i]);
        if (!name) continue;
        char *full = malloc(VP_PATH + 300);
        if (!full) continue;
        snprintf(full, VP_PATH + 300, "%s\\%s", app.cur_dir, name->host_name);
        paths[path_count++] = full;
        if (name->is_dir) dirs++; else files++;
        vp_remove(&after, app.cur_dir, name->host_name);
    }
    if (!path_count) { set_status(L"지울 항목을 먼저 고르세요."); vp_free(&after); free(paths); return; }
    VpFit fit_after; vp_check(&after, (uint32_t)app.size_mb, app.kind, &fit_after);
    uint32_t freed_kb = app.fit.used_kb > fit_after.used_kb ? app.fit.used_kb - fit_after.used_kb : 0;
    wchar_t freed[32], head[120], content[500];
    format_mb(freed, 32, freed_kb);
    swprintf(head, 120, L"선택한 %d개를 지울까요?", path_count);
    swprintf(content, 500, L"파일 %d개, 폴더 %d개(폴더 안의 것 포함)를 지웁니다.\n지우면 약 %ls 가 비워집니다.\n\n휴지통을 거치지 않아 되돌릴 수 없습니다.", files, dirs, freed);
    const wchar_t *labels[1] = { L"지우기" };
    int choice = ask_choice(head, content, labels, 1, L"취소");
    if (choice == 0) {
        set_busy(1); set_status(L"지우는 중…"); pump_messages();
        JobReport report; run_delete((const char *const *)paths, path_count, app.vhd_root, &report);
        set_busy(0); refresh_all(); refresh_drive_labels();
        set_status_fmt(report.failed ? L"일부를 지우지 못했습니다 (%d건)." : L"지웠습니다. 남은 용량이 늘었습니다.", report.failed);
        if (report.failed) { wchar_t detail[400]; text_to_wide(report.first_error, detail, 400); show_message(L"지우지 못한 항목이 있습니다", detail); }
    } else set_status(L"삭제를 취소했습니다.");
    for (int i = 0; i < path_count; i++) free(paths[i]);
    free(paths); vp_free(&after);
}
static void open_in_explorer(void)
{
    if (!app.cur_dir[0]) return;
    wchar_t path[VP_PATH]; text_to_wide(app.cur_dir, path, VP_PATH);
    ShellExecuteW(app.main, L"open", path, NULL, NULL, SW_SHOWNORMAL);
}

/* ---------- 콤보 바뀜 ---------- */
static void on_size_or_kind_changed(void)
{
    int size_item = (int)SendMessageW(app.size_combo, CB_GETCURSEL, 0, 0);
    int kind_item = (int)SendMessageW(app.kind_combo, CB_GETCURSEL, 0, 0);
    if (size_item >= 0) app.size_mb = (int)SendMessageW(app.size_combo, CB_GETITEMDATA, size_item, 0);
    if (kind_item >= 0) app.kind = kind_item;
    recheck_capacity(); fill_list();
}

/* ---------- 창 ---------- */
static void create_children(void)
{
    app.usb_combo = make_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_USB);
    app.btn_refresh = make_control(L"BUTTON", L"새로 고침", BS_PUSHBUTTON | WS_TABSTOP, ID_REFRESH);
    app.drive_combo = make_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_DRIVE);
    app.size_combo = make_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, ID_SIZE);
    app.kind_combo = make_control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, ID_KIND);
    { HWND combos[] = { app.usb_combo, app.drive_combo, app.size_combo, app.kind_combo }; for (int i = 0; i < 4; i++) SendMessageW(combos[i], CB_SETITEMHEIGHT, (WPARAM)-1, S(26)); }
    for (unsigned i = 0; i < sizeof size_choices / sizeof size_choices[0]; i++) {
        wchar_t text[24]; swprintf(text, 24, L"%d MB", size_choices[i]);
        int at = (int)SendMessageW(app.size_combo, CB_ADDSTRING, 0, (LPARAM)text);
        SendMessageW(app.size_combo, CB_SETITEMDATA, at, size_choices[i]);
    }
    for (int i = 0; i < 4; i++) SendMessageW(app.kind_combo, CB_ADDSTRING, 0, (LPARAM)kind_labels[i]);
    app.btn_up = make_control(L"BUTTON", L"▲ 위 폴더", BS_PUSHBUTTON | WS_TABSTOP, ID_UP);
    app.path_label = make_control(L"STATIC", L"", SS_PATHELLIPSIS | SS_NOPREFIX, ID_PATH);
    app.list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0, 10, 10, app.main, (HMENU)(INT_PTR)ID_LIST, GetModuleHandleW(NULL), NULL);
    set_control_font(app.list, app.font);
    ListView_SetImageList(app.list, ImageList_Create(1, S(30), ILC_COLOR, 1, 1), LVSIL_SMALL);   /* 줄 높이를 키우는 빈 그림 목록 */
    ListView_SetExtendedListViewStyle(app.list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    static const wchar_t *const headings[] = { L"이름", L"MSX 에서 보이는 이름", L"크기", L"종류" };
    for (int i = 0; i < 4; i++) { LVCOLUMNW col; memset(&col, 0, sizeof col); col.mask = LVCF_TEXT | LVCF_WIDTH | (i == 2 ? LVCF_FMT : 0); col.fmt = LVCFMT_RIGHT; col.cx = S(100); col.pszText = (LPWSTR)headings[i]; ListView_InsertColumn(app.list, i, &col); }
    app.btn_add_files = make_control(L"BUTTON", L"파일 추가…", BS_PUSHBUTTON | WS_TABSTOP, ID_ADD_FILES);
    app.btn_add_folder = make_control(L"BUTTON", L"폴더 추가…", BS_PUSHBUTTON | WS_TABSTOP, ID_ADD_FOLDER);
    app.btn_delete = make_control(L"BUTTON", L"선택 삭제", BS_PUSHBUTTON | WS_TABSTOP, ID_DELETE);
    app.btn_open = make_control(L"BUTTON", L"탐색기로 열기", BS_PUSHBUTTON | WS_TABSTOP, ID_OPEN_EXPLORER);
    app.btn_stop = make_control(L"BUTTON", L"중지", BS_PUSHBUTTON | WS_TABSTOP, ID_STOP);
    app.status = make_control(L"STATIC", L"파일을 이 창에 끌어다 놓으면 용량을 계산해서 넣어 줍니다.", SS_PATHELLIPSIS | SS_NOPREFIX, ID_STATUS);
    app.progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | PBS_SMOOTH, 0, 0, 10, 10, app.main, (HMENU)(INT_PTR)ID_PROGRESS, GetModuleHandleW(NULL), NULL);
    SendMessageW(app.progress, PBM_SETRANGE32, 0, 1000);
    ShowWindow(app.btn_stop, SW_HIDE);
}
static void create_fonts(void)
{
    LOGFONTW lf; memset(&lf, 0, sizeof lf);
    wcscpy(lf.lfFaceName, L"Malgun Gothic"); lf.lfCharSet = DEFAULT_CHARSET; lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfHeight = -S(16); app.font = CreateFontIndirectW(&lf);
    lf.lfHeight = -S(14); app.font_small = CreateFontIndirectW(&lf);
    lf.lfHeight = -S(16); lf.lfWeight = FW_BOLD; app.font_bold = CreateFontIndirectW(&lf);
    lf.lfHeight = -S(30); app.font_big = CreateFontIndirectW(&lf);
}
static void on_command(int id, int notify)
{
    switch (id) {
    case ID_USB: if (notify == CBN_SELCHANGE) on_usb_selected(); else if (notify == CBN_DROPDOWN) refresh_usb_list(); break;
    case ID_DRIVE:
        if (notify == CBN_SELCHANGE) select_drive_item((int)SendMessageW(app.drive_combo, CB_GETCURSEL, 0, 0));
        else if (notify == CBN_DROPDOWN) refresh_drive_labels();
        break;
    case ID_SIZE: case ID_KIND: if (notify == CBN_SELCHANGE) on_size_or_kind_changed(); break;
    case ID_REFRESH: if (app.vhd_root[0]) { refresh_usb_list(); refresh_all(); refresh_drive_labels(); } else find_usb_automatically(); break;
    case ID_UP: go_up(); break;
    case ID_ADD_FILES: add_files_by_dialog(); break;
    case ID_ADD_FOLDER: add_folder_by_dialog(); break;
    case ID_DELETE: delete_selected(); break;
    case ID_OPEN_EXPLORER: open_in_explorer(); break;
    case ID_STOP: app.cancel_requested = 1; break;
    }
}
static int row_is_folder(int row)                               /* "위 폴더" 줄도 폴더로 친다 */
{
    int hasUpRow = _stricmp(app.cur_dir, app.vhd_root) != 0;
    if (hasUpRow && row == 0) return 1;
    const VpName *name = name_for_row(row);
    return name && name->is_dir;
}
static LRESULT paint_list_row_color(NMLVCUSTOMDRAW *draw)       /* 폴더 줄만 글자색을 바꿈 (고른 줄은 기본 색 그대로) */
{
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
        int selected = (ListView_GetItemState(app.list, (int)draw->nmcd.dwItemSpec, LVIS_SELECTED) & LVIS_SELECTED) != 0;
        if (!selected && row_is_folder((int)draw->nmcd.dwItemSpec)) draw->clrText = COLOR_FOLDER_TEXT;
    }
    return CDRF_DODEFAULT;
}
static LRESULT on_notify(NMHDR *header)
{
    if (header->idFrom != ID_LIST) return 0;
    if (header->code == NM_CUSTOMDRAW) return paint_list_row_color((NMLVCUSTOMDRAW *)header);
    if (header->code == NM_DBLCLK) { int row = ((NMITEMACTIVATE *)header)->iItem; if (row >= 0) enter_row(row); }
    else if (header->code == LVN_KEYDOWN) {
        WORD key = ((NMLVKEYDOWN *)header)->wVKey;
        if (key == VK_DELETE) delete_selected();
        else if (key == VK_BACK) go_up();
        else if (key == VK_RETURN) { int row = ListView_GetNextItem(app.list, -1, LVNI_SELECTED); if (row >= 0) enter_row(row); }
        else if (key == VK_F5) refresh_all();
    }
    return 0;
}
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: app.main = hwnd; SetTimer(hwnd, TIMER_WATCH_ID, TIMER_WATCH_MS, NULL); return 0;
    case WM_SIZE: if (app.progress) layout_controls(LOWORD(lp), HIWORD(lp)); InvalidateRect(hwnd, NULL, FALSE); return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *m = (MINMAXINFO *)lp; m->ptMinTrackSize.x = S(940); m->ptMinTrackSize.y = S(760); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps); RECT r; GetClientRect(hwnd, &r);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, r.right, r.bottom); HGDIOBJ old = SelectObject(mem, bmp);
        paint_window(mem, r.right, r.bottom);
        BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem); EndPaint(hwnd, &ps); return 0;
    }
    case WM_CTLCOLORSTATIC: { HDC dc = (HDC)wp; SetBkColor(dc, COLOR_WINDOW_BG); SetTextColor(dc, (HWND)lp == app.status ? COLOR_MUTED : COLOR_TEXT); static HBRUSH bg; if (!bg) bg = CreateSolidBrush(COLOR_WINDOW_BG); return (LRESULT)bg; }
    case WM_COMMAND: on_command(LOWORD(wp), HIWORD(wp)); return 0;
    case WM_NOTIFY: return on_notify((NMHDR *)lp);
    case WM_DROPFILES: if (!app.busy) on_drop_files((HDROP)wp); else DragFinish((HDROP)wp); return 0;
    case WM_CLOSE: if (app.busy) { app.cancel_requested = 1; return 0; } DestroyWindow(hwnd); return 0;
    case WM_TIMER: if (wp == TIMER_WATCH_ID) on_watch_timer(); return 0;
    case WM_DESTROY: KillTimer(hwnd, TIMER_WATCH_ID); stop_watching_folder(); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, PWSTR cmdline, int show)
{
    (void)prev;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    HDC screen = GetDC(NULL); app.dpi = GetDeviceCaps(screen, LOGPIXELSX); ReleaseDC(NULL, screen);
    create_fonts();
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc; wc.lpfnWndProc = window_proc; wc.hInstance = instance; wc.lpszClassName = L"X86VHD_MAIN";
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1)); wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
    wchar_t title[64]; swprintf(title, 64, L"X86VHD V%hs - X86MSX 가상 하드 관리", X86VHD_VERSION);
    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, S(1220), S(900), NULL, NULL, instance, NULL);
    app.main = hwnd;
    create_children();
    RECT client; GetClientRect(hwnd, &client); layout_controls(client.right, client.bottom);
    ShowWindow(hwnd, show); UpdateWindow(hwnd);
    if (!use_command_line_root(cmdline)) find_usb_automatically();
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    vp_free(&app.tree); free(app.names);
    CoUninitialize();
    return 0;
}
