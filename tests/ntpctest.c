/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* ntpctest.c - 호스트 SNTP 클라이언트(src/ntpc.inc) 시험. 가짜 DNS/UDP 로 정상·실패·서버 바꾸기·시간 변환을 본다. */
#include "../src/ntpc.inc"
#include <stdio.h>
#include <stdlib.h>

static int mode, netdown, dns_n, sent, rdy; static uint32_t unix_t; static char names[8][40]; static int nn;
static int h_state(void *c) { (void)c; return netdown ? NC_NET_DOWN : NC_NET_UP; }
static int h_dns_start(void *c, const char *n, int l) { (void)c; if (nn < 8) { memcpy(names[nn], n, l); names[nn][l] = 0; nn++; } dns_n = 0; return 0; }
static int h_dns_result(void *c, uint8_t *st, uint8_t ip[4], uint8_t *err)
{
    (void)c; *err = 0;
    int first = (nn == 1);
    if (mode == 3 || (mode == 5 && first)) { *st = NC_DNS_FAIL; return 0; }
    if (mode == 6) { *st = NC_DNS_PENDING; return 0; }                /* 영원히 대기 */
    if (++dns_n < 3) { *st = NC_DNS_PENDING; return 0; }
    *st = NC_DNS_OK; ip[0] = 1; ip[1] = 2; ip[2] = 3; ip[3] = 4; return 0;
}
static int h_open(void *c, uint16_t lp, uint8_t *h) { (void)c; (void)lp; *h = 0; return 0; }
static int h_close(void *c, int h) { (void)c; (void)h; return 0; }
static int h_send(void *c, int h, const uint8_t rip[4], uint16_t rp, const uint8_t *d, int l)
{
    (void)c; (void)h; (void)rip; sent++;
    if (rp != 123 || l != 48 || d[0] != 0x23) return -1;
    if (mode == 1) rdy = 0; else if (mode == 7 && nn < 2) rdy = 0; else rdy = 1;   /* 7: 첫 서버는 답이 없고 둘째만 답함 */
    return 0;
}
static int h_recv(void *c, int h, uint8_t *dst, int max, uint8_t sip[4], uint16_t *sp, int *got)
{
    (void)c; (void)h; (void)max; (void)sip; (void)sp;
    if (!rdy) return NC_E_NODATA;
    rdy = 0;
    uint32_t t = unix_t + 2208988800u;
    memset(dst, 0, 48);
    dst[0] = (mode == 4) ? 0x23 : 0x24; dst[1] = (mode == 8) ? 0 : 2;
    dst[40] = t >> 24; dst[41] = t >> 16; dst[42] = t >> 8; dst[43] = t;
    *got = 48; return 0;
}
static NetHost H;
static int run(int m, uint32_t ut, int *res_reason, uint32_t *out_elapsed)
{
    mode = m; unix_t = ut; netdown = (m == 2); nn = 0; sent = 0; rdy = 0; dns_n = 0;
    NtpC c; uint32_t now = 1000; ntpc_start(&c, &H, m == 9 ? "my.server.kr" : "", now);
    int r; int guard = 0;
    while ((r = ntpc_poll(&c, now)) == NTPC_RUN) { now += 100; if (++guard > 5000) { printf("hang\n"); exit(1); } }
    if (res_reason) *res_reason = c.reason;
    if (out_elapsed) *out_elapsed = now - 1000;
    if (r == NTPC_OK && c.unix_utc != ut) { printf("bad unix %u\n", c.unix_utc); exit(1); }
    return r;
}
static int fail;
#define CHK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fail = 1; } } while (0)
int main(void)
{
    memset(&H, 0, sizeof H);
    H.net_state = h_state; H.dns_start = h_dns_start; H.dns_result = h_dns_result;
    H.udp_open = h_open; H.udp_close = h_close; H.udp_send = h_send; H.udp_recv = h_recv;
    int why; uint32_t el;
    CHK(run(0, 1793491200u, &why, &el) == NTPC_OK, "normal");                    /* 2026-11-01 00:00:00 UTC */
    CHK(!strcmp(names[0], "ntp.kriss.re.kr") && nn == 1, "default server = KRISS");
    CHK(run(9, 1793491200u, 0, 0) == NTPC_OK && !strcmp(names[0], "my.server.kr"), "user server first");
    CHK(run(1, 1793491200u, &why, &el) == NTPC_FAIL && why == 3, "no answer -> fail");
    CHK(nn == 4 && sent == 4 * NTPC_TRIES, "tried 4 servers x2 (default list)");
    CHK(el < 30000, "fail within 20 s");
    CHK(run(2, 1793491200u, &why, 0) == NTPC_FAIL && why == 1, "net down");
    CHK(run(3, 1793491200u, &why, 0) == NTPC_FAIL && why == 2, "dns fail");
    CHK(run(4, 1793491200u, &why, 0) == NTPC_FAIL, "wrong mode ignored");
    CHK(run(8, 1793491200u, &why, 0) == NTPC_FAIL, "stratum 0 ignored");
    CHK(run(5, 1793491200u, 0, 0) == NTPC_OK && nn == 2, "dns fail on first -> second server works");
    CHK(run(7, 1793491200u, 0, 0) == NTPC_OK && nn == 2, "first no answer -> second answers");
    CHK(run(6, 1793491200u, &why, &el) == NTPC_FAIL && why == 2, "dns never answers");
    CHK(run(0, 1000u, 0, 0) == NTPC_FAIL, "year < 2020 rejected");
    /* 날짜 변환 */
    int t[6];
    ntpc_civil(1793491200u + 9 * 3600, t);  CHK(t[0] == 2026 && t[1] == 11 && t[2] == 1 && t[3] == 9 && t[4] == 0 && t[5] == 0, "KST 2026-11-01 09:00");
    ntpc_civil(1709164800u, t);             CHK(t[0] == 2024 && t[1] == 2 && t[2] == 29 && t[3] == 0, "leap day 2024-02-29");
    ntpc_civil(1709164799u + 1, t);         CHK(t[1] == 2 && t[2] == 29, "leap 2");
    ntpc_civil(1709251200u, t);             CHK(t[1] == 3 && t[2] == 1, "2024-03-01");
    ntpc_civil(1798675199u, t);             CHK(t[0] == 2026 && t[1] == 12 && t[2] == 30 && t[3] == 23 && t[4] == 59 && t[5] == 59, "2026-12-30 23:59:59");
    ntpc_civil(1798675199u + 86400u, t);    CHK(t[0] == 2026 && t[1] == 12 && t[2] == 31, "12-31");
    ntpc_civil(1798675199u + 2*86400u, t);  CHK(t[0] == 2027 && t[1] == 1 && t[2] == 1, "new year");
    ntpc_civil(1793491200u - 5*3600 - 1800, t); CHK(t[2] == 31 && t[3] == 18 && t[4] == 30, "tz -5:30 style negative offset (prev day)");
    CHK(ntpc_rtc_secs(315532800u) == 0, "rtc epoch 1980");
    CHK(ntpc_rtc_secs(315532800u + 86400u) == 86400u, "rtc +1 day");
    printf(fail ? "ntpctest: FAIL\n" : "ntpctest: ok\n");
    return fail;
}
