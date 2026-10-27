/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
#ifndef NETSTACK_H
#define NETSTACK_H
#include <stdint.h>
struct NetHost;
void net_set_flush(void (*f)(void));
void net_set_guard(void (*f)(int), int skip);
void net_lease_set(const uint8_t ip[4]);   /* 마지막으로 받은 주소 (0.0.0.0 = 없음) — 다음 DHCP 는 INIT-REBOOT */
void net_set_kind(int k);                 /* net_start 전에 — NK_WIRED / NK_WIFI / 0 */
void net_start(uint32_t ms);      /* 한 번: 랜카드 찾기·lwIP 시작 */
void net_poll(uint32_t ms);       /* 매 프레임: 받기·타이머 */
int  net_has_nic(void);
int  net_hw_present(void);        /* 랜카드가 있는지 PCI 읽기만으로 (스택 시작 전에도) */
void net_halt(void);              /* 재부팅·전원 끄기 직전: 랜카드를 완전히 멈춘다 */
void net_stop(void);              /* 끄기·재시작 전에 랜카드 DMA를 멈춘다 */
int  net_online(void);            /* 1 = IP 받음 */
int  net_log_take(char *out, int max);
uint32_t net_rand(void);
void net_config(int manual, const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4], const uint8_t dns[4]);   /* 수동 주소 / DHCP */
int  net_ipinfo(uint8_t out[20]); /* 0 = 성공 (IP, 마스크, 게이트웨이, DNS1, DNS2) */
void net_renew(void);             /* 주소가 없으면 DHCP(또는 수동 값)를 다시 */
int  net_state(void);
void net_diag_text(char *o, int max);   /* 옵션 창 진단 줄: 링크·DHCP 단계·보낸/받은 프레임 수 */             /* 0 = 없음·끊김, 1 = 주소 기다림, 2 = 연결됨 */
const struct NetHost *nethost_get(void);   /* MSX 네트워크 카트리지용 호스트 (nethost_lwip.c) */
int  nethost_poll(void);          /* 매 프레임: 1 = 알릴 일이 생김 → msx_net_event */
#endif
