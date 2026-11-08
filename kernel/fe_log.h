/* 만든이: KimTaeWoo
 * 도와준이: Lee.J.H
 * X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
 * fe_log.h (8.0.129) - 진단 로그 스위치: 분야마다 스위치 하나 (진단 > 스위치 탭, ini Log*, X86MSX.SW).
 * 옛 이름은 매크로로 이어 붙임 (호출하는 쪽을 안 고침). 분야 순서 = 이 열거형 순서 */
#ifndef FE_LOG_H
#define FE_LOG_H
enum { LA_USB, LA_KBD, LA_SND, LA_MOUSE, LA_JOY, LA_TP, LA_VID, LA_WIFI, LA_LAN, LA_COLOR, LA_BOOT, LA_N };
extern int log_area[LA_N];
#define log_boot      log_area[LA_BOOT]     /* LOG\BOOT_X·DRV_X·LASTHANG·HANG·*_HDMI (부팅·드라이버·멈춤 기록, 8.0.134) */
#define log_key       log_area[LA_KBD]      /* LOG\KEY_X<판>.TXT */
#define log_sndlog    log_area[LA_SND]      /* LOG\SNDLOG.TXT */
#define log_sndglitch log_area[LA_SND]      /* LOG\SNDGLITCH.TXT (소리 분야 하나로 합침) */
#define log_stall     log_area[LA_VID]      /* LOG\STALL.TXT (화면 분야) */
#endif
