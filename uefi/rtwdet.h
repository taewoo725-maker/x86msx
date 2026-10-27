/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * rtwdet.h - RTL8822CE 1b단계: 메모리 접근을 잠시 켜서 식별 레지스터만 읽고 원상 복구
 * 쓰는 곳은 PCI CMD(MEM+BM)와 PM D-state뿐이며 읽은 뒤 원래 값으로 되돌린다. UEFI 전용.
 * 근거: linux rtw88 reg.h (REG_SYS_CFG1=0xF0 등), pci.c (pci_enable_device/set_master)
 */
#ifndef RTWDET_H
#define RTWDET_H
#include <stdint.h>
int rtwdet_info(char (*lines)[64], int max);   /* "wifi rtw ..." 줄 (WiFi 탭). 최초 1회만 실제 접근 */
int rtwdet_power(char *buf, int max, const uint8_t *fw, uint32_t fwsz, void *dma, uint32_t dmasz, int dl, const uint8_t *phy, uint32_t phyn);       /* 1단계 칩 전원 켜기. 결과 로그 글자 수를 돌려줌 (DRV\RTW.INI POWER=1 일 때만 호출) */
/* 세 토막 - begin(PCI 설정 접근, 메인 코어에서) → work(칩 메모리만 접근, 다른 코어에서 돌려도 됨) → end(PCI 설정 복구, 메인 코어에서) */
int rtwdet_begin(char *buf, int max, const uint8_t *fw, uint32_t fwsz, void *dma, uint32_t dmasz, int dl, const uint8_t *phy, uint32_t phyn);   /* 1=work 가능, 0=실패(로그만 남음) */
void rtwdet_work(void);
int rtwdet_end(void);          /* 로그 전체 글자 수 */
int rtwdet_loglen(void);
void rtwdet_log(const char *s);
void rtwdet_setcred(const char *ssid, const char *pass);   /* 접속할 공유기 이름·비밀번호 (RTW.INI) */
void rtwdet_live_enable(uint32_t (*clk)(void));   /* 실시간 모드 켬 (begin 성공 뒤). clk = ms 시계. 이후 netstack(Nic nic_rtw)이 step/poll 로 돌림 */
int rtwdet_ui_scan_begin(void);                                     /* 찾기 시작 (약 20초 계속, 목록이 채워짐) */
int rtwdet_ui_scanning(void);
void rtwdet_ui_scan_stop(void);
int rtwdet_ui_scan_get(char ssid[][36], int *sig, int *sec, int max);
int rtwdet_ui_connect(const char *ssid, const char *pass);          /* 0 = 요청함 */
int rtwdet_ui_state(void);                                          /* 0 미지정 1 연결 중 2 연결됨 3 공유기 못 찾음 4 접속 실패 */
#endif
