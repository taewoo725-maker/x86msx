/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* nic.h - 레거시(BIOS) 판 랜카드 드라이버 공통 인터페이스 (5.0.94~). 드라이버 = 칩 하나당 파일 하나.
 * 규칙: 알아보는 PCI ID에만 반응한다 (모르는 장치는 건드리지 않음 → 인증된 PC에 영향 없음). 모든 단계는 net_log()로 먼저 기록. */
#ifndef NIC_H
#define NIC_H
#include <stdint.h>
typedef struct Nic {
    const char *name;
    int  (*probe)(void);                 /* 1 = 이 칩을 찾았고 초기화함 (실패하면 0, 이유는 net_log에) */
    void (*mac)(uint8_t out[6]);
    int  (*link)(void);                  /* 1 = 케이블 연결됨 */
    int  (*tx)(const uint8_t *f, int n); /* 0 = 보냄 */
    int  (*rx)(uint8_t *f, int max);     /* 받은 길이, 0 = 없음 */
    void (*diag)(void);                  /* 상태를 net_log로 (없으면 0) */
    void (*stop)(void);                  /* 끄기·재시작 전에 DMA를 멈춘다 (없으면 0) */
    int  (*step)(void);                  /* 초기화를 나눠 하는 칩: 프레임마다 부름. 0 = 아직, 1 = 끝, -1 = 실패 (없으면 0 = probe가 다 했음) */
    int  (*poll)(void);                  /* 초기화 뒤에도 프레임마다 부름 (무선: 접속 유지·재접속을 조금씩 진행). 없으면 0 */
    uint16_t mtu;                        /* 0 = 1500 */
} Nic;
void net_log(const char *s);             /* 한 줄 기록 ("NET.xxx key=val", 줄 끝은 자동) */
void net_log_hex(const char *key, uint32_t v);
/* PCI 설정 공간 (포트 방식) */
uint32_t nic_pci_rd(int b, int d, int f, int off);
void nic_pci_wr(int b, int d, int f, int off, uint32_t v);
void net_guard(int on);                  /* 1 = 위험한 단계 시작(표시 파일 씀), 0 = 무사히 지남 */
int  net_skip_risky(void);               /* 1 = 지난번 위험 단계에서 멈췄음 → 건너뜀 */
uint32_t net_now(void);                  /* 지금 시각(ms) */
void nic_udelay(uint32_t us);            /* 대략 us 마이크로초 (포트 0x80 쓰기) */
int  nic_pci_find(uint16_t ven, uint16_t dev, int *b, int *d, int *f);
extern const Nic nic_rtw;                /* 5.0.157 UEFI 1번 Lenovo V14-ARE 무선 10EC:C822 (uefi/rtwdet.c) */
extern const Nic nic_r8152;              /* 5.2.110 UEFI USB 유선 Realtek RTL8152/8153 (uefi/usbnic_r8152.c) */
extern const Nic nic_asix;               /* 5.2.117 UEFI USB 유선 ASIX AX88772/AX88179 (uefi/usbnic_asix.c, 미시험) */
extern const Nic nic_bcm57785;           /* 5번 Acer Aspire V5-171 유선 14E4:16B5 */
#endif
