/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * device.h - 장치 한 줄 틀 DeviceOps (5.2.118 구조 개편 5단계 나머지 2)
 *
 * 한 장치가 I/O 버스에서 하는 모든 일을 표 한 줄에 모은다: 이름, 가질 수 있는 포트, 이 기계에 있는 조건, 읽기, 쓰기.
 * 예전에는 이것이 io_ports() switch, io_present() switch, io_rd_fn[]·io_wr_fn[] 배열 네 군데로 흩어져 있었다 (msx.c).
 * 새 I/O 장치 = IoDev 번호 + 이 표에 IODEV(...) 한 줄 + 읽기/쓰기 함수. 사슬 만들기·IN/OUT 코드는 안 고친다.
 *
 * 속도: IN/OUT 은 예전처럼 사슬(번호)을 앞에서부터 물어 표의 rd/wr 를 부른다 (간접 호출 한 번, 같음).
 * ports·present 는 사슬을 다시 만들 때만 불리는 느린 길.
 * 메모리 칸 장치는 MemOps (아래, memdev.inc 의 mem_dev[], 5.2.119). 상태 덩이 MsxStDev 는 장치가 아니라 MSX 구조체 필드 단위라 따로 둠.
 */
#ifndef MSX_DEVICE_H
#define MSX_DEVICE_H
#include <stdint.h>
#include "msx.h"

typedef struct DeviceOps {
    const char *name;                                   /* 진단·시험용 이름 */
    int (*ports)(int port);                             /* 켜짐 여부와 무관하게 이 장치가 가질 수 있는 포트인가 (고정 범위) */
    int (*present)(const MSX *m);                       /* 이 기계에 이 장치가 있는가 (기종·꽂은 카트). 0(NULL) = 늘 있음 */
    int (*rd)(MSX *m, int port, uint8_t *v);            /* 읽기: 1 = 내 것 (*v 에 값), 0 = 아님. NULL = 읽기 없음 */
    int (*wr)(MSX *m, int port, uint8_t v);             /* 쓰기: 1 = 처리 끝, 0 = 다음 장치로. NULL = 쓰기 없음 */
} DeviceOps;

/* 메모리 칸(8 KiB) 특수 장치 한 줄 (memdev.inc 의 mem_dev[]) — 같은 장치의 메모리 쪽 면. 레지스터가 메모리에 보이는 장치만.
 *   rd_claim/wr_claim(m, b, ps, ss): 슬롯이 바뀔 때 — 이 칸에 내가 보이면 0 아닌 값(보통 카트 번호+1). NULL = 읽기/쓰기에 안 낌
 *   rd(m, arg, a): 읽기 — 내 것이면 0~255, 아니면 -1.  wr(m, arg, a, v): 쓰기 — 1 = 처리 끝, 0 = 다음 장치로 */
typedef struct MemOps {
    const char *name;
    int (*rd_claim)(const MSX *m, int b, int ps, int ss);
    int (*wr_claim)(const MSX *m, int b, int ps, int ss);
    int (*rd)(MSX *m, int arg, uint16_t a);
    int (*wr)(MSX *m, int arg, uint16_t a, uint8_t v);
} MemOps;
#endif
