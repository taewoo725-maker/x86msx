/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * ntpc.inc (8.0.49) - 호스트(PC) 쪽 SNTP 클라이언트. NetHost(DNS·UDP)로 인터넷 시간을 받아 온다.
 * fe.c 와 시험(tests/ntpctest.c)이 #include 한다. 상태 기계라서 ntpc_poll() 을 틱마다 부르면 된다.
 *
 *   ntpc_start(&c, host, "time.google.com", now_ms);
 *   while ((r = ntpc_poll(&c, now_ms)) == NTPC_RUN) ...;     // NTPC_OK 면 c.unix_utc 가 UTC 유닉스 초
 */
#ifndef NTPC_INC
#define NTPC_INC
#include <stdint.h>
#include <string.h>
#include "netcard.h"

enum { NTPC_IDLE = 0, NTPC_RUN = 1, NTPC_OK = 2, NTPC_FAIL = 3 };
#define NTPC_SERVERS 4
#define NTPC_TRY_MS  1500u                     /* 요청 하나를 기다리는 시간 */
#define NTPC_TRIES   2                         /* 서버 하나에 요청을 보내는 횟수 */
#define NTPC_DNS_MS  4000u                     /* 이름 찾기를 기다리는 시간 */

typedef struct {
    const NetHost *h;
    int st;                                    /* NTPC_* */
    int stage;                                 /* 0 이름 찾기 1 요청 보냄 */
    int srv, tries;
    char name[NTPC_SERVERS][40];
    int nname;
    int udp;                                   /* 열린 UDP 핸들 (-1 = 없음) */
    uint32_t t0;                               /* 이 단계를 시작한 시각 (ms) */
    uint32_t unix_utc;                         /* 결과: UTC 유닉스 초 */
    int reason;                                /* 실패 이유: 1 네트워크 없음 2 이름 못 찾음 3 응답 없음 */
} NtpC;

static void ntpc_close(NtpC *c) { if (c->udp >= 0 && c->h && c->h->udp_close) c->h->udp_close(c->h->ctx, c->udp); c->udp = -1; }

static void ntpc_add(NtpC *c, const char *s)
{
    if (!s || !s[0] || c->nname >= NTPC_SERVERS) return;
    for (int i = 0; i < c->nname; i++) if (!strcmp(c->name[i], s)) return;
    size_t n = strlen(s); if (n > 39) n = 39;
    memcpy(c->name[c->nname], s, n); c->name[c->nname][n] = 0; c->nname++;
}

/* 사용자가 정한 서버(비면 ntp.kriss.re.kr)를 먼저, 안 되면 time.google.com · kr.pool.ntp.org · pool.ntp.org 를 차례로 */
static void ntpc_start(NtpC *c, const NetHost *h, const char *server, uint32_t now)
{
    memset(c, 0, sizeof *c);
    c->h = h; c->udp = -1;
    ntpc_add(c, server && server[0] ? server : "ntp.kriss.re.kr");   /* 한국표준과학연구원 */
    ntpc_add(c, "time.google.com"); ntpc_add(c, "kr.pool.ntp.org"); ntpc_add(c, "pool.ntp.org");
    c->st = NTPC_RUN; c->t0 = now;
}

static void ntpc_fail(NtpC *c, int why) { ntpc_close(c); c->st = NTPC_FAIL; c->reason = why; }

static void ntpc_next_server(NtpC *c, uint32_t now)
{
    ntpc_close(c);
    if (++c->srv >= c->nname) { ntpc_fail(c, c->reason ? c->reason : 3); return; }
    c->stage = 0; c->tries = 0; c->t0 = now;
}

static int ntpc_poll(NtpC *c, uint32_t now)
{
    const NetHost *h = c->h;
    if (c->st != NTPC_RUN) return c->st;
    if (!h || !h->net_state || h->net_state(h->ctx) != NC_NET_UP || !h->dns_start || !h->dns_result || !h->udp_open || !h->udp_send || !h->udp_recv) { ntpc_fail(c, 1); return c->st; }
    if (c->stage == 0) {                                           /* 이름 → IP */
        if (c->tries == 0) {                                       /* 이 서버의 첫 호출 */
            if (h->dns_start(h->ctx, c->name[c->srv], (int)strlen(c->name[c->srv])) != 0) { c->reason = 2; ntpc_next_server(c, now); return c->st; }
            c->tries = 1; c->t0 = now;
        }
        uint8_t stt = 0, ip[4] = { 0, 0, 0, 0 }, err = 0;
        if (h->dns_result(h->ctx, &stt, ip, &err) != 0 || stt == NC_DNS_FAIL) { c->reason = 2; ntpc_next_server(c, now); return c->st; }
        if (stt == NC_DNS_OK) {
            uint8_t hd = 0;
            if (h->udp_open(h->ctx, (uint16_t)(40123 + c->srv), &hd) != 0) { c->reason = 3; ntpc_next_server(c, now); return c->st; }
            c->udp = hd; memcpy(&c->unix_utc, ip, 4);              /* 잠깐 IP 보관 (unix_utc 는 끝나면 덮어씀) */
            c->stage = 1; c->tries = 0; c->t0 = now - NTPC_TRY_MS;   /* 곧바로 첫 요청 */
        } else if (now - c->t0 >= NTPC_DNS_MS) { c->reason = 2; ntpc_next_server(c, now); }
        return c->st;
    }
    /* stage 1: 요청 보내고 기다리기 */
    {
        uint8_t ip[4]; memcpy(ip, &c->unix_utc, 4);
        uint8_t rb[64]; uint8_t sip[4]; uint16_t sp = 0; int got = 0;
        int rc = h->udp_recv(h->ctx, c->udp, rb, sizeof rb, sip, &sp, &got);
        if (rc == 0 && got >= 48) {
            uint8_t mode = rb[0] & 7, li = rb[0] >> 6;
            uint32_t tx = ((uint32_t)rb[40] << 24) | ((uint32_t)rb[41] << 16) | ((uint32_t)rb[42] << 8) | rb[43];
            if ((mode == 4 || mode == 5) && li != 3 && rb[1] != 0 && tx > 2208988800u + 1577836800u) {   /* 서버 응답, 시계 동기 상태, 층 1~15, 2020년 이후 */
                c->unix_utc = tx - 2208988800u; ntpc_close(c); c->st = NTPC_OK; return c->st;
            }
        }
        if (now - c->t0 >= NTPC_TRY_MS) {
            if (c->tries >= NTPC_TRIES) { c->reason = 3; ntpc_next_server(c, now); return c->st; }
            static const uint8_t req0 = 0x23;
            uint8_t rq[48]; memset(rq, 0, sizeof rq); rq[0] = req0;
            if (h->udp_send(h->ctx, c->udp, ip, 123, rq, 48) != 0) { c->reason = 3; ntpc_next_server(c, now); return c->st; }
            c->tries++; c->t0 = now;
        }
    }
    return c->st;
}

/* 지역 유닉스 초 → 연·월·일·시·분·초 */
static void ntpc_civil(uint32_t t, int out[6])
{
    uint32_t days = t / 86400u, r = t % 86400u; int y = 1970;
    for (;;) { uint32_t yl = ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 366u : 365u; if (days < yl) break; days -= yl; y++; }
    static const uint8_t md[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int mo = 0;
    for (;;) { uint32_t ml = md[mo] + (mo == 1 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)); if (days < ml) break; days -= ml; mo++; }
    out[0] = y; out[1] = mo + 1; out[2] = (int)days + 1; out[3] = (int)(r / 3600); out[4] = (int)(r / 60 % 60); out[5] = (int)(r % 60);
}
/* 지역 유닉스 초 → MSX RTC 초 (1980-01-01 기준) */
static uint32_t ntpc_rtc_secs(uint32_t local_unix) { return local_unix - 315532800u; }
#endif
