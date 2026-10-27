/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * icons.c - 아이콘 이름 (USB의 Icons\이름.png). 본체에 든 그림은 icons_hd.c (tools/mkicons_hd.py)
 */
#include "icons.h"

const char *const icon_name[ICON_COUNT] = {
    "CAPS", "KANA", "FDD", "CAS", "HD", "CART", "SWITCH", "MIXER", "KEYBOARD", "GEAR",
    "PSG", "SCC", "PCM", "M-M", "M-A", "M-S", "OPP", "MIDI", "HANGUL", "FOLDER", "FILE", "POINTER",
    "RESET", "POWER", "SPEED", "MONITOR", "JOY", "SAVE", "LOAD", "CAMERA", "MOUSE", "PAUSE", "MENU", "SCAN", "MEDIA",   /* 3.5.0 */
    "PSG2",                                                                                  /* 3.6.7 Joyrex */
    "SOFTMIDI", "USBMIDI", "ETC", "ZIP", "HDD", "PRINTER", "Z80", "R800", "TEXT", "STATE", "SOUND", "DISKIMG", "OTHER", "DSK360", "DSK144", "DSK720", "ARROWR", "ARROWL", "PICTURE", "MOVIE", "NET", "V9990",                                                            /* 3.7.0 (SFG.png → OPP.png) */
};
