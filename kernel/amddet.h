/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * amddet.h - AMD 디스플레이 출력 선택 1단계: PCI 열거 (읽기 전용)
 *
 * 그래픽 장치(클래스 03)와 무선랜(클래스 0280)을 찾아 진단 화면에 보고한다.
 * 하드웨어에 쓰지 않으므로 안전. 값 확인 뒤 2단계(HPD·EDID)에서 사용.
 */
#ifndef AMDDET_H
#define AMDDET_H
/* 진단 정보: 찾은 그래픽·무선랜 장치와 BAR. 줄 수 반환 */
int amddet_info(char (*lines)[64], int max);
int amddet_wifi_info(char (*lines)[64], int max);   /* 4.0.34: 8번 탭(WiFi) */
#endif
