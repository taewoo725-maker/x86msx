/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* winhost.h - X86VHD V1.0.0: Windows 쪽 바닥 일 (UTF-8 ↔ UTF-16, 폴더 목록 호스트, x86msx.ini 의 가상 하드 설정, USB 찾기) */
#ifndef X86VHD_WINHOST_H
#define X86VHD_WINHOST_H
#include <windows.h>
#include "vhdplan.h"

#define VHD_SLOTS 6                                     /* C: ~ H: */

typedef struct { char folder[VP_PATH]; int size_mb; int kind; } VhdSlotSetting;   /* folder 는 VMFolder 아래 이름 ("" = 연결 안 함) */

void text_to_wide(const char *utf8, wchar_t *out, int cap);
void wide_to_text(const wchar_t *wide, char *out, int cap);
const VfHost *windows_host(void);                       /* vp_scan 에 줄 호스트 (목록만 읽음) */

/* USB 뿌리(예: "E:\")에서 x86msx.ini 를 읽어 C~H 설정을 채움. 파일이 없으면 -1 */
int read_vhd_settings(const char *usb_root, VhdSlotSetting slots[VHD_SLOTS]);
/* 폴더 안 X86MSX.VHD 에서 Size(MB)/Format 을 읽음. 파일이 없으면 -1 (= 가상 하드가 아님) */
int read_vhd_folder_config(const char *folder_path, int *size_mb, int *kind);
/* x86msx.ini 나 Storage Device\VMFolder 가 있는 드라이브(X86MSX USB)만 찾아 "E:\" 형태로 돌려줌. 없으면 0 */
int find_usb_roots(char roots[][8], int max);
int is_x86msx_root(const char *usb_root);                           /* X86MSX 가 든 USB 뿌리인가 */
void vmfolder_path(const char *usb_root, char *out, int cap);       /* "E:\Storage Device\VMFolder" */
#endif
