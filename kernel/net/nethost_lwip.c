/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* nethost_lwip.c - 레거시(BIOS) 판: MSX 네트워크 카트리지(NetHost)를 lwIP(BSD-3)에 연결한다. 5.0.108
 * UEFI 쪽 uefi/netuefi.c 와 같은 규칙(NC_* 코드, 핸들 4+4개, 닫힌 연결의 남은 자료는 MSX가 읽을 때까지 보관). */
#include <string.h>
#include "netstack.h"
#include "nic.h"
#include "../../src/netcard.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/dns.h"
#include "lwip/raw.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/icmp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"

#define FIFO  (TCP_WND + 512)             /* 창보다 크게 → 넘칠 수 없다 */
#define UQ    4
enum { CS_FREE, CS_CONN, CS_OPEN, CS_DEAD };

typedef struct { struct tcp_pcb *pcb; uint8_t cs, peer_fin, reason, rip[4]; uint16_t lport, rport; int head, count; uint8_t fifo[FIFO]; } Conn;
typedef struct { uint16_t len, port; uint8_t ip[4]; uint8_t data[NETCARD_UDP_MAX]; } Dg;
typedef struct { struct udp_pcb *pcb; uint8_t used; uint16_t lport; int head, count; Dg q[UQ]; } Usock;

static Conn conn[NETCARD_MAX_CONN];
static Usock usk[NETCARD_MAX_UDP];
static int pending_ev;
static uint16_t eph_t = 0, eph_u = 0;

/* DNS */
static int dns_pend, dns_state = NC_DNS_IDLE, dns_gen, dns_started_ms; static uint8_t dns_ipb[4], dns_errb;
extern uint32_t sys_now(void);

void nethost_note(int lost)
{
    pending_ev = 1;
    if (!lost) return;
    for (int i = 0; i < NETCARD_MAX_CONN; i++) {
        Conn *c = &conn[i];
        if (c->cs == CS_CONN || c->cs == CS_OPEN) {
            if (c->pcb) { tcp_arg(c->pcb, 0); tcp_abort(c->pcb); c->pcb = 0; }
            c->reason = NC_REASON_NETLOST; c->cs = CS_DEAD;
        }
    }
    if (dns_pend) { dns_pend = 0; dns_gen++; dns_state = NC_DNS_FAIL; dns_errb = NC_DNSERR_NETLOST; }
}
int nethost_poll(void) { int e = pending_ev; pending_ev = 0; return e; }

/* ---- TCP ---- */
static err_t t_recv(void *a, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    Conn *c = (Conn *)a; (void)pcb; (void)err;
    if (!c) { if (p) pbuf_free(p); return ERR_OK; }
    if (!p) { c->peer_fin = 1; pending_ev = 1; return ERR_OK; }
    for (struct pbuf *q = p; q; q = q->next) {
        int n = q->len, sp = FIFO - c->count; if (n > sp) n = sp;
        for (int i = 0; i < n; i++) c->fifo[(c->head + c->count + i) % FIFO] = ((uint8_t *)q->payload)[i];
        c->count += n;
    }
    pbuf_free(p); pending_ev = 1;
    return ERR_OK;
}
static void t_err(void *a, err_t e)
{
    Conn *c = (Conn *)a; if (!c) return;
    c->pcb = 0;                           /* lwIP가 이미 풀었다 */
    c->reason = e == ERR_RST ? NC_REASON_REFUSED : e == ERR_ABRT ? NC_REASON_TIMEOUT : e == ERR_RTE ? NC_REASON_UNREACH : NC_REASON_OTHER;
    c->cs = CS_DEAD; pending_ev = 1;
}
static err_t t_conn(void *a, struct tcp_pcb *pcb, err_t err)
{
    Conn *c = (Conn *)a; (void)pcb; (void)err;
    if (c) { c->cs = CS_OPEN; pending_ev = 1; }
    return ERR_OK;
}
static uint16_t pick_t(void)
{
    for (int tries = 0; tries < 64; tries++) {
        if (eph_t < 16384 || eph_t > 32767) eph_t = (uint16_t)(16384 + net_rand() % 16384);
        uint16_t p = eph_t++; int used = 0;
        for (int i = 0; i < NETCARD_MAX_CONN; i++) if (conn[i].cs != CS_FREE && conn[i].lport == p) used = 1;
        if (!used) return p;
    }
    return eph_t;
}
static int tcp_open_cb(void *ctx, uint16_t lport, const uint8_t rip[4], uint16_t rport, uint8_t flags, uint8_t *handle)
{
    (void)ctx;
    if (net_state() != 2) return NC_E_NETDOWN;
    if (flags & NC_OPEN_PASSIVE) return NC_E_BADARG;
    int i; for (i = 0; i < NETCARD_MAX_CONN; i++) if (conn[i].cs == CS_FREE) break;
    if (i == NETCARD_MAX_CONN) return NC_E_NOSLOT;
    Conn *c = &conn[i];
    struct tcp_pcb *pcb = tcp_new(); if (!pcb) return NC_E_NOSLOT;
    err_t e = ERR_USE;
    for (int t = 0; t < 8 && e == ERR_USE; t++) {
        uint16_t lp = lport ? lport : pick_t();
        e = tcp_bind(pcb, IP_ANY_TYPE, lp);
        if (e == ERR_OK) lport = lp; else if (lport) break;
    }
    if (e != ERR_OK) { tcp_abort(pcb); return e == ERR_USE ? NC_E_INUSE : NC_E_HOST; }
    memset(c, 0, sizeof *c);
    c->pcb = pcb; c->lport = lport; c->rport = rport; memcpy(c->rip, rip, 4);
    tcp_arg(pcb, c); tcp_recv(pcb, t_recv); tcp_err(pcb, t_err);
    ip_addr_t ra; IP_ADDR4(&ra, rip[0], rip[1], rip[2], rip[3]);
    c->cs = CS_CONN;
    if (tcp_connect(pcb, &ra, rport, t_conn) != ERR_OK) { tcp_arg(pcb, 0); tcp_abort(pcb); memset(c, 0, sizeof *c); return NC_E_HOST; }
    *handle = (uint8_t)i;
    return 0;
}
static void conn_clear(Conn *c) { memset(c, 0, sizeof *c); }
static int tcp_close_cb(void *ctx, int h, int abort)
{
    (void)ctx; Conn *c = &conn[h];
    if (c->cs == CS_FREE) return 0;
    if (c->pcb) {
        struct tcp_pcb *p = c->pcb;
        tcp_arg(p, 0); tcp_recv(p, 0);
        if (c->count) tcp_recved(p, (u16_t)c->count);       /* 안 읽은 자료가 있어도 창을 되돌려 RST 대신 정상 닫기 */
        if (abort || tcp_close(p) != ERR_OK) tcp_abort(p);
        c->pcb = 0;
    }
    conn_clear(c);
    return 0;
}
static int tcp_state_cb(void *ctx, int h, uint8_t *state, uint16_t *rx_avail, uint16_t *tx_free, uint8_t *reason)
{
    (void)ctx; Conn *c = &conn[h];
    if (c->cs == CS_FREE) return NC_E_NOHANDLE;
    *state = c->cs == CS_CONN ? NC_TCP_SYN_SENT : c->cs == CS_OPEN ? (c->peer_fin ? NC_TCP_CLOSE_WAIT : NC_TCP_ESTABLISHED) : NC_TCP_CLOSED;
    *rx_avail = (uint16_t)(c->count > 65535 ? 65535 : c->count);
    *tx_free = (c->cs == CS_OPEN && c->pcb) ? (uint16_t)tcp_sndbuf(c->pcb) : 0;
    *reason = c->reason;
    return 0;
}
static int tcp_send_cb(void *ctx, int h, const uint8_t *data, int len, int push, int *accepted)
{
    (void)ctx; Conn *c = &conn[h]; *accepted = 0;
    if (c->cs != CS_OPEN || !c->pcb) return NC_E_NOHANDLE;
    int n = len < (int)tcp_sndbuf(c->pcb) ? len : (int)tcp_sndbuf(c->pcb);
    if (n <= 0) return 0;
    if (tcp_write(c->pcb, data, (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK) return 0;   /* 큐가 찼다 → 0바이트 받음, 나중에 다시 */
    if (push) tcp_output(c->pcb);
    *accepted = n;
    return 0;
}
static int tcp_recv_cb(void *ctx, int h, uint8_t *dst, int maxlen, int *got)
{
    (void)ctx; Conn *c = &conn[h];
    if (c->cs == CS_FREE) return NC_E_NOHANDLE;
    int n = c->count < maxlen ? c->count : maxlen;
    for (int i = 0; i < n; i++) dst[i] = c->fifo[(c->head + i) % FIFO];
    c->head = (c->head + n) % FIFO; c->count -= n; *got = n;
    if (n && c->pcb) tcp_recved(c->pcb, (u16_t)n);
    return 0;
}
static int tcp_flush_cb(void *ctx, int h)
{
    (void)ctx; Conn *c = &conn[h];
    if (c->cs == CS_FREE) return NC_E_NOHANDLE;
    if (c->pcb) tcp_output(c->pcb);
    return 0;
}
static int tcp_info_cb(void *ctx, int h, uint8_t out[8])
{
    (void)ctx; Conn *c = &conn[h];
    if (c->cs == CS_FREE) return NC_E_NOHANDLE;
    memcpy(out, c->rip, 4);
    out[4] = (uint8_t)c->rport; out[5] = (uint8_t)(c->rport >> 8); out[6] = (uint8_t)c->lport; out[7] = (uint8_t)(c->lport >> 8);
    return 0;
}

/* ---- DNS ---- */
static void dns_found(const char *name, const ip_addr_t *ip, void *arg)
{
    (void)name;
    if ((int)(intptr_t)arg != dns_gen || !dns_pend) return;       /* 취소된 질의 */
    dns_pend = 0; pending_ev = 1;
    if (ip) { uint32_t v = ip4_addr_get_u32(ip_2_ip4(ip)); memcpy(dns_ipb, &v, 4); dns_state = NC_DNS_OK; }
    else {                                                          /* lwIP는 이유를 알려 주지 않는다 → 오래 걸렸으면 "답 없음", 빨리 끝났으면 "없는 이름" */
        dns_state = NC_DNS_FAIL;
        dns_errb = (sys_now() - (uint32_t)dns_started_ms > 3000) ? NC_DNSERR_NOREPLY : NC_DNSERR_NXDOMAIN;
    }
}
static int dns_start_cb(void *ctx, const char *name, int len)
{
    (void)ctx; char b[256];
    if (net_state() != 2) return NC_E_NETDOWN;
    if (dns_pend) return NC_E_NOSLOT;
    if (len < 1 || len > 255) return NC_E_BADARG;
    for (int i = 0; i < len; i++) { uint8_t ch = (uint8_t)name[i]; if (ch < 0x21 || ch > 0x7E) return NC_E_BADARG; b[i] = (char)ch; }
    b[len] = 0;
    ip_addr_t r; dns_gen++; dns_started_ms = (int)sys_now(); dns_errb = 0;
    err_t e = dns_gethostbyname(b, &r, dns_found, (void *)(intptr_t)dns_gen);
    if (e == ERR_OK) { uint32_t v = ip4_addr_get_u32(ip_2_ip4(&r)); memcpy(dns_ipb, &v, 4); dns_state = NC_DNS_OK; dns_pend = 0; pending_ev = 1; return 0; }
    if (e == ERR_INPROGRESS) { dns_pend = 1; dns_state = NC_DNS_PENDING; return 0; }
    return NC_E_HOST;
}
static int dns_result_cb(void *ctx, uint8_t *status, uint8_t ip[4], uint8_t *err)
{
    (void)ctx;
    *status = dns_pend ? NC_DNS_PENDING : (uint8_t)dns_state;
    if (*status == NC_DNS_OK) memcpy(ip, dns_ipb, 4); else memset(ip, 0, 4);
    *err = *status == NC_DNS_FAIL ? dns_errb : 0;
    return 0;
}
static int dns_cancel_cb(void *ctx) { (void)ctx; dns_pend = 0; dns_gen++; dns_state = NC_DNS_IDLE; return 0; }

/* ---- UDP ---- */
static void u_recv(void *a, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    Usock *u = (Usock *)a; (void)pcb;
    if (u && u->count < UQ && p) {
        Dg *d = &u->q[(u->head + u->count) % UQ];
        int n = p->tot_len > NETCARD_UDP_MAX ? NETCARD_UDP_MAX : p->tot_len;
        pbuf_copy_partial(p, d->data, (u16_t)n, 0);
        d->len = (uint16_t)n; d->port = port;
        uint32_t v = ip4_addr_get_u32(ip_2_ip4(addr)); memcpy(d->ip, &v, 4);
        u->count++; pending_ev = 1;
    }
    if (p) pbuf_free(p);
}
static int udp_open_cb(void *ctx, uint16_t lport, uint8_t *handle)
{
    (void)ctx;
    if (net_state() != 2) return NC_E_NETDOWN;
    int i; for (i = 0; i < NETCARD_MAX_UDP; i++) if (!usk[i].used) break;
    if (i == NETCARD_MAX_UDP) return NC_E_NOSLOT;
    if (lport) for (int k = 0; k < NETCARD_MAX_UDP; k++) if (usk[k].used && usk[k].lport == lport) return NC_E_INUSE;
    struct udp_pcb *pcb = udp_new(); if (!pcb) return NC_E_NOSLOT;
    err_t e = ERR_USE;
    for (int t = 0; t < 8 && e == ERR_USE; t++) {
        uint16_t lp = lport;
        if (!lp) { if (eph_u < 16384 || eph_u > 32767) eph_u = (uint16_t)(16384 + net_rand() % 16384); lp = eph_u++; }
        e = udp_bind(pcb, IP_ANY_TYPE, lp);
        if (e == ERR_OK) lport = lp; else if (lport) break;
    }
    if (e != ERR_OK) { udp_remove(pcb); return e == ERR_USE ? NC_E_INUSE : NC_E_HOST; }
    Usock *u = &usk[i]; memset(u, 0, sizeof *u);
    u->pcb = pcb; u->used = 1; u->lport = lport;
    udp_recv(pcb, u_recv, u);
    *handle = (uint8_t)i;
    return 0;
}
static int udp_close_cb(void *ctx, int h)
{
    (void)ctx; Usock *u = &usk[h];
    if (!u->used) return NC_E_NOHANDLE;
    udp_recv(u->pcb, 0, 0); udp_remove(u->pcb); memset(u, 0, sizeof *u);
    return 0;
}
static int udp_state_cb(void *ctx, int h, uint8_t *count, uint16_t *oldest, uint16_t *lport)
{
    (void)ctx; Usock *u = &usk[h];
    if (!u->used) return NC_E_NOHANDLE;
    *count = (uint8_t)u->count; *oldest = u->count ? u->q[u->head].len : 0; *lport = u->lport;
    return 0;
}
static int udp_send_cb(void *ctx, int h, const uint8_t rip[4], uint16_t rport, const uint8_t *data, int len)
{
    (void)ctx; Usock *u = &usk[h];
    if (!u->used) return NC_E_NOHANDLE;
    if (net_state() != 2) return NC_E_NETDOWN;
    if (len < 0 || len > NETCARD_UDP_MAX) return NC_E_BADARG;
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (!p) return NC_E_NOSLOT;
    if (len) pbuf_take(p, data, (u16_t)len);
    ip_addr_t ra; IP_ADDR4(&ra, rip[0], rip[1], rip[2], rip[3]);
    err_t e = udp_sendto(u->pcb, p, &ra, rport);
    pbuf_free(p);
    return e == ERR_OK ? 0 : (e == ERR_RTE ? NC_E_NETDOWN : NC_E_HOST);
}
static int udp_recv_cb(void *ctx, int h, uint8_t *dst, int maxlen, uint8_t srcip[4], uint16_t *srcport, int *got)
{
    (void)ctx; Usock *u = &usk[h];
    if (!u->used) return NC_E_NOHANDLE;
    if (!u->count) return NC_E_NODATA;
    Dg *d = &u->q[u->head];
    int n = d->len < maxlen ? d->len : maxlen;
    memcpy(dst, d->data, (size_t)n); memcpy(srcip, d->ip, 4); *srcport = d->port; *got = n;
    u->head = (u->head + 1) % UQ; u->count--;
    return 0;
}

/* ---- ICMP 에코 (PING, 5.0.131) ---- */
#define EQ 4
typedef struct { uint8_t ip[4], ttl; uint16_t id, seq, len; } Echo;
static struct raw_pcb *echo_pcb; static Echo eq_[EQ]; static int eq_head, eq_count;
static u8_t echo_rx(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
    (void)arg; (void)pcb; (void)addr;
    if (p->len < 28) return 0;                       /* IP 머리(20 이상) + ICMP 머리 8 */
    const uint8_t *ip = (const uint8_t *)p->payload; unsigned ihl = (unsigned)(ip[0] & 15) * 4;
    if (ip[9] != 1 || p->len < ihl + 8) return 0;
    const uint8_t *ic = ip + ihl;
    if (ic[0] != 0 || ic[1] != 0) return 0;          /* 에코 응답만 */
    if (eq_count == EQ) return 0;                    /* 가득이면 버린다 (규격 허용) */
    Echo *e = &eq_[(eq_head + eq_count) % EQ];
    memcpy(e->ip, ip + 12, 4); e->ttl = ip[8];
    e->id = (uint16_t)((ic[4] << 8) | ic[5]); e->seq = (uint16_t)((ic[6] << 8) | ic[7]);
    unsigned tot = (unsigned)((ip[2] << 8) | ip[3]); e->len = (uint16_t)(tot > ihl + 8 ? tot - ihl - 8 : 0);
    eq_count++; pending_ev = 1;
    return 0;                                        /* lwIP도 계속 처리 */
}
static int echo_send_cb(void *ctx, const uint8_t rip[4], uint8_t ttl, uint16_t id, uint16_t seq, uint16_t len)
{
    (void)ctx;
    if (net_state() != 2) return NC_E_NETDOWN;
    if (!echo_pcb) {
        echo_pcb = raw_new(IP_PROTO_ICMP);
        if (!echo_pcb) return NC_E_NOSLOT;
        raw_recv(echo_pcb, echo_rx, 0);
        raw_bind(echo_pcb, IP_ADDR_ANY);
    }
    struct pbuf *p = pbuf_alloc(PBUF_IP, (u16_t)(8 + len), PBUF_RAM);
    if (!p) return NC_E_NOSLOT;
    uint8_t *b = (uint8_t *)p->payload;
    b[0] = 8; b[1] = 0; b[2] = b[3] = 0;
    b[4] = (uint8_t)(id >> 8); b[5] = (uint8_t)id; b[6] = (uint8_t)(seq >> 8); b[7] = (uint8_t)seq;
    for (unsigned i = 0; i < len; i++) b[8 + i] = (uint8_t)i;
    u16_t ck = inet_chksum(b, (u16_t)(8 + len));
    b[2] = (uint8_t)(ck & 0xFF); b[3] = (uint8_t)(ck >> 8);   /* inet_chksum은 네트워크 순서로 접어 둔 값을 메모리 순서 그대로 쓰면 된다 */
    echo_pcb->ttl = ttl ? ttl : 255;
    ip_addr_t ra; IP_ADDR4(&ra, rip[0], rip[1], rip[2], rip[3]);
    err_t e = raw_sendto(echo_pcb, p, &ra);
    pbuf_free(p);
    return e == ERR_OK ? 0 : (e == ERR_RTE ? NC_E_NETDOWN : NC_E_HOST);
}
static int echo_recv_cb(void *ctx, uint8_t out[11])
{
    (void)ctx;
    if (!eq_count) return NC_E_NODATA;
    Echo *e = &eq_[eq_head];
    memcpy(out, e->ip, 4); out[4] = e->ttl;
    out[5] = (uint8_t)e->id; out[6] = (uint8_t)(e->id >> 8); out[7] = (uint8_t)e->seq; out[8] = (uint8_t)(e->seq >> 8);
    out[9] = (uint8_t)e->len; out[10] = (uint8_t)(e->len >> 8);
    eq_head = (eq_head + 1) % EQ; eq_count--;
    return 0;
}

static int h_state(void *ctx) { (void)ctx; return net_state() == 2 ? NC_NET_UP : net_state() == 1 ? NC_NET_SETUP : NC_NET_DOWN; }
static int h_ipinfo(void *ctx, uint8_t out[20]) { (void)ctx; return net_ipinfo(out); }
static NetHost host;
const NetHost *nethost_get(void)
{
    if (!host.net_state) {
        host.net_state = h_state; host.get_ipinfo = h_ipinfo;
        host.dns_start = dns_start_cb; host.dns_result = dns_result_cb; host.dns_cancel = dns_cancel_cb;
        host.tcp_open = tcp_open_cb; host.tcp_close = tcp_close_cb; host.tcp_state = tcp_state_cb; host.tcp_send = tcp_send_cb;
        host.tcp_recv = tcp_recv_cb; host.tcp_flush = tcp_flush_cb; host.tcp_info = tcp_info_cb;
        host.udp_open = udp_open_cb; host.udp_close = udp_close_cb; host.udp_state = udp_state_cb; host.udp_send = udp_send_cb; host.udp_recv = udp_recv_cb;
        host.echo_send = echo_send_cb; host.echo_recv = echo_recv_cb;
    }
    return &host;
}
