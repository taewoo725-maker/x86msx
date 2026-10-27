/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* hdadiag.h - HDMI 소리 진단 (4.0.62): HD Audio 컨트롤러·코덱·핀·ELD, Intel 디스플레이 오디오 레지스터를 읽어 LOG\HDA_DIAG.TXT 로 저장.
 * 읽기 위주: 컨트롤러가 리셋 상태면 리셋을 풀고(CRST=1) 코덱에 get 동사만 보낸다. 소리는 내지 않는다. */
#ifndef HDADIAG_H
#define HDADIAG_H
#include <stdint.h>
typedef struct {
    uint32_t (*pci_rd)(int b, int d, int f, int off);
    void (*stall_us)(uint32_t us);
    void (*logw)(const char *path, const uint8_t *d, uint32_t n);
} HdaDiagIo;
int hdadiag_run(const HdaDiagIo *io);      /* 찾은 HDA 컨트롤러 수 */
#endif
