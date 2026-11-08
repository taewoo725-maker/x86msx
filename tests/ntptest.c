/* 만든이: KimTaeWoo */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * ntptest.c - X86NTP.COM 시험: 실제 unapi.rom + netcard + 가짜 호스트(DNS/UDP) + 가짜 시간 서버.
 * 사용법: ntptest X86NTP.COM unapi.rom "명령줄" unix초 [mode] [동작]
 *   mode: 0 정상, 1 서버무응답, 2 네트워크 다운, 3 DNS 실패, 4 모드 틀린 응답
 */
#include "../src/z80.h"
#include "../src/netcard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t mem[65536];
static NetCard nc;
static int mode;
static uint32_t unix_t;
static int dns_polls, sent, last_port, last_len, rcv_ready;
static uint8_t last_req0;
static char dns_name[80];
static int bdos_date, bdos_time, y, mo, d, hh, mi, ss;

static int h_state(void *c) { (void)c; return mode == 2 ? NC_NET_DOWN : NC_NET_UP; }
static int h_dns_start(void *c, const char *n, int l) { (void)c; memcpy(dns_name, n, l); dns_name[l] = 0; dns_polls = 0; return 0; }
static int h_dns_result(void *c, uint8_t *st, uint8_t ip[4], uint8_t *err)
{
    (void)c; *err = 0;
    if (mode == 3) { *st = NC_DNS_FAIL; *err = 1; return 0; }
    if (++dns_polls < 3) { *st = NC_DNS_PENDING; return 0; }
    *st = NC_DNS_OK; ip[0] = 211; ip[1] = 1; ip[2] = 2; ip[3] = 3; return 0;
}
static int h_udp_open(void *c, uint16_t lp, uint8_t *h) { (void)c; (void)lp; *h = 0; return 0; }
static int h_udp_close(void *c, int h) { (void)c; (void)h; return 0; }
static int h_udp_send(void *c, int h, const uint8_t rip[4], uint16_t rp, const uint8_t *data, int len)
{
    (void)c; (void)h; (void)rip;
    sent++; last_port = rp; last_len = len; last_req0 = data[0];
    if (mode != 1) rcv_ready = 2;
    return 0;
}
static int h_udp_recv(void *c, int h, uint8_t *dst, int max, uint8_t sip[4], uint16_t *sp, int *got)
{
    (void)c; (void)h; (void)max;
    if (rcv_ready > 0) { if (--rcv_ready > 0) return NC_E_NODATA; } else return NC_E_NODATA;
    uint32_t t = unix_t + 2208988800u;
    memset(dst, 0, 48);
    dst[0] = (mode == 4) ? 0x23 : 0x24;
    dst[1] = 2;
    dst[40] = t >> 24; dst[41] = t >> 16; dst[42] = t >> 8; dst[43] = t;
    sip[0] = 211; sip[1] = 1; sip[2] = 2; sip[3] = 3; *sp = 123; *got = 48;
    return 0;
}

static uint8_t mread(void *c, uint16_t a) { (void)c; return mem[a]; }
static void mwrite(void *c, uint16_t a, uint8_t v) { (void)c; if (a >= 0x4000 && a < 0x7F00) return; mem[a] = v; }
static uint64_t cyc;
static uint8_t ioread(void *c, uint16_t p) { (void)c; p &= 0xFF; if (p >= 0x28 && p <= 0x2B) return netcard_io_read(&nc, p, cyc); return 0xFF; }
static void iowrite(void *c, uint16_t p, uint8_t v) { (void)c; p &= 0xFF; if (p >= 0x28 && p <= 0x2B) netcard_io_write(&nc, p, v, cyc); }

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
    size_t n = fread(mem + 0x100, 1, 0x7000, f); fclose(f); (void)n;
    f = fopen(argv[2], "rb"); if (!f) { perror(argv[2]); return 2; }
    if (fread(mem + 0x4000, 1, 0x4000, f) != 0x4000) return 2; fclose(f);
    unix_t = (uint32_t)strtoul(argv[4], 0, 10);
    mode = argc > 5 ? atoi(argv[5]) : 0;
    int entry = 0x402c;
    mem[0] = 0x76; mem[5] = 0xC9;
    mem[0x80] = (uint8_t)strlen(argv[3]); memcpy(mem + 0x81, argv[3], strlen(argv[3])); mem[0x81 + strlen(argv[3])] = 0;

    netcard_init(&nc, 0x28);
    NetHost h; memset(&h, 0, sizeof h);
    h.net_state = h_state; h.dns_start = h_dns_start; h.dns_result = h_dns_result;
    h.udp_open = h_udp_open; h.udp_close = h_udp_close; h.udp_send = h_udp_send; h.udp_recv = h_udp_recv;
    netcard_set_host(&nc, &h);
    nc.enabled = 1;

    Z80 z; z80_init(&z);
    z.mem_read = mread; z.mem_write = mwrite; z.io_read = ioread; z.io_write = iowrite;
    z.pc = 0x100; z.sp.w = 0xF000;
    uint64_t steps = 0;
    for (;;) {
        if (z.pc == 0x0005) {
            int fn = z.bc.b.l;
            if (fn == 9) { for (uint16_t a = z.de.w; mem[a] != '$'; a++) putchar(mem[a]); }
            else if (fn == 2) putchar(z.de.b.l);
            else if (fn == 0x2B) { bdos_date++; y = z.hl.w; mo = z.de.b.h; d = z.de.b.l; z.af.b.h = 0; }
            else if (fn == 0x2D) { bdos_time++; hh = z.hl.b.h; mi = z.hl.b.l; ss = z.de.b.h; z.af.b.h = 0; }
        }
        if (z.pc == 0x001C) { z.pc = z.ix.w; /* CALSLT: 같은 슬롯 안에서 IX 로 */ }
        if (z.pc == 0xFFCA) {   /* EXTBIO: 규격 식별 */
            int a = z.af.b.h;
            if (a == 0) { z.bc.b.h++; }
            else if (a == 1) { z.af.b.h = 0x8F; z.bc.b.h = 0xFF; z.hl.w = (uint16_t)entry; }
            z.pc = mem[z.sp.w] | (mem[z.sp.w + 1] << 8); z.sp.w += 2;
        }
        if (z.pc == 0x0000 || (z.halted && z.pc == 1)) break;
        z80_step(&z);
        cyc += 4; netcard_tick(&nc, cyc);
        if (z.halted) { z.halted = 0; cyc += 20000; netcard_tick(&nc, cyc); }
        if (++steps > 50000000ull) { printf("\nTIMEOUT\n"); return 3; }
    }
    printf("\n[dns=%s sent=%d port=%d len=%d b0=%02X date=%d time=%d] SET %04d-%02d-%02d %02d:%02d:%02d\n",
           dns_name, sent, last_port, last_len, last_req0, bdos_date, bdos_time, y, mo, d, hh, mi, ss);
    if (getenv("DUMP")) { int a = (int)strtol(getenv("DUMP"), 0, 16); printf("dump@%X:", a); for (int i = 0; i < 24; i++) printf(" %02X", mem[a + i]); printf("\n"); }
    return 0;
}
