/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* netstack.c - 레거시(BIOS) 판 네트워크: lwIP(BSD-3) + 랜카드 드라이버(nic.h) 연결, DHCP. 5.0.94~
 * 기록은 NET_LOG 버퍼에 모아 net_log_take()로 가져가 LOG\NET_X<ver>.TXT 에 쓴다. */
#include "nic.h"
#include "nicdb.h"
#include "netstack.h"
#include <string.h>
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/etharp.h"
#include "lwip/pbuf.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "netif/ethernet.h"

static char lg[14000]; static int lgn; static uint32_t now_ms;
static void (*flush_hook)(void);
static void (*guard_hook)(int); static int skip_risky;
void net_set_guard(void (*f)(int), int skip) { guard_hook = f; skip_risky = skip; }
void net_guard(int on) { if (guard_hook) guard_hook(on); }
int net_skip_risky(void) { return skip_risky; }
void net_set_flush(void (*f)(void)) { flush_hook = f; }
static inline void po8(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
uint32_t net_now(void) { return now_ms; }
void nic_udelay(uint32_t us) { while (us--) po8(0x80, 0); }
static int has_step(const char *s) { for (; s[0] && s[1] && s[2] && s[3] && s[4]; s++) if (s[0]=='s'&&s[1]=='t'&&s[2]=='e'&&s[3]=='p'&&s[4]=='=') return 1; return 0; }
void net_log(const char *s)
{
    int n = (int)strlen(s);
    if (lgn + n + 12 > (int)sizeof lg) return;
    char t[12]; uint32_t v = now_ms; int k = 0; char r[11];
    do { r[k++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int i = 0; i < k; i++) t[i] = r[k - 1 - i];
    t[k++] = ' ';
    memcpy(lg + lgn, t, (size_t)k); lgn += k;
    memcpy(lg + lgn, s, (size_t)n); lgn += n;
    lg[lgn++] = '\r'; lg[lgn++] = '\n';
    if (flush_hook && has_step(s)) flush_hook();       /* 멈춰도 마지막 단계가 남도록 */
}
void net_log_hex(const char *key, uint32_t v)
{
    char b[80]; int n = (int)strlen(key); if (n > 60) n = 60;
    memcpy(b, key, (size_t)n); b[n++] = '='; b[n++] = '0'; b[n++] = 'x';
    for (int i = 7; i >= 0; i--) b[n++] = "0123456789ABCDEF"[(v >> (4 * i)) & 15];
    b[n] = 0; net_log(b);
}
int net_log_take(char *out, int max)
{
    int n = lgn < max ? lgn : max; memcpy(out, lg, (size_t)n); return n;
}
void net_assert_fail(const char *m) { net_log("LWIP.assert"); net_log(m ? m : "?"); }
uint32_t sys_now(void) { return now_ms; }
static uint32_t rs = 0x1234567u;
uint32_t net_rand(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
/* 난수 씨앗을 부팅마다 다르게 (CPU 시간 카운터 + MAC). 이전엔 고정이라 DHCP xid·TCP 순번·포트가 부팅마다 똑같았음 — RFC 2131 (xid 는 무작위), RFC 6528 */
static void net_seed(const uint8_t mac[6])
{
    uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    rs ^= lo ^ (hi << 7) ^ (uint32_t)now_ms * 2654435761u;
    for (int i = 0; i < 6; i++) rs = (rs ^ mac[i]) * 16777619u;
    if (!rs) rs = 0x1234567u;
    for (int i = 0; i < 8; i++) (void)net_rand();
}


/* ---- PCI (포트 방식) ---- */
static inline void po32(uint16_t p, uint32_t v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint32_t pi32(uint16_t p) { uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static uint32_t pa(int b, int d, int f, int o) { return 0x80000000u | (uint32_t)b << 16 | (uint32_t)d << 11 | (uint32_t)f << 8 | (uint32_t)(o & 0xFC); }
uint32_t nic_pci_rd(int b, int d, int f, int o) { po32(0xCF8, pa(b, d, f, o)); return pi32(0xCFC); }
void nic_pci_wr(int b, int d, int f, int o, uint32_t v) { po32(0xCF8, pa(b, d, f, o)); po32(0xCFC, v); }
int nic_pci_find(uint16_t ven, uint16_t dev, int *pb, int *pd, int *pf)
{
    for (int b = 0; b < 8; b++) for (int d = 0; d < 32; d++) for (int f = 0; f < 8; f++) {
        uint32_t id = nic_pci_rd(b, d, f, 0);
        if (id == 0xFFFFFFFFu) { if (f == 0) break; continue; }
        if ((id & 0xFFFF) == ven && (id >> 16) == dev) { *pb = b; *pd = d; *pf = f; return 1; }
        if (f == 0 && !(nic_pci_rd(b, d, 0, 0x0C) & 0x800000u)) break;       /* 단일 기능 장치 */
    }
    return 0;
}

/* ---- lwIP netif ---- */
static const Nic *nic;
static struct netif nif;
static uint8_t fbuf[1600];
static uint32_t dhcp_t0;                  /* 주소 받기를 시작한(다시 시작한) 시각 — 1분 넘게 못 받으면 윈도우처럼 처음부터 다시 */
static int lwip_ready, started, got_ip, last_link = -1; static uint32_t diag_next, diag_left;

static uint32_t tx_ok, tx_bad, rx_cnt, wait_next; static int wait_left = 60;
static err_t out_cb(struct netif *n, struct pbuf *p)
{
    (void)n;
    if (p->tot_len > sizeof fbuf) return ERR_BUF;
    pbuf_copy_partial(p, fbuf, p->tot_len, 0);
    if (nic->tx(fbuf, (int)p->tot_len) == 0) { tx_ok++; return ERR_OK; }
    tx_bad++; return ERR_IF;
}
static err_t if_init(struct netif *n)
{
    n->name[0] = 'e'; n->name[1] = 'n';
    n->output = etharp_output; n->linkoutput = out_cb;
    n->mtu = nic->mtu ? nic->mtu : 1500; n->hwaddr_len = 6;     /* 무선은 송신 슬롯이 작아 600 */
    n->hwaddr_len = 6;
    nic->mac(n->hwaddr);
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;
    return ERR_OK;
}
extern ip4_addr_t x86msx_dhcp_reboot_ip;                  /* third_party/lwip dhcp.c */
static uint8_t lease_ip[4];                               /* 마지막으로 받은 주소 (설정 파일에서 옴). 있으면 DHCP 를 INIT-REBOOT 로 시작 */
void net_lease_set(const uint8_t ip[4]) { memcpy(lease_ip, ip, 4); }
static void lease_arm(void)
{
    if (lease_ip[0] | lease_ip[1] | lease_ip[2] | lease_ip[3]) { IP4_ADDR(&x86msx_dhcp_reboot_ip, lease_ip[0], lease_ip[1], lease_ip[2], lease_ip[3]); net_log("NET.dhcp init-reboot (request last address)"); }
}
static int cfg_manual; static uint8_t cfg_ip[4], cfg_mask[4], cfg_gw[4], cfg_dns[4];
extern void nethost_note(int lost);
static void st_cb(struct netif *n)
{
    int was = got_ip;
    if (!ip4_addr_isany_val(*netif_ip4_addr(n))) {
        got_ip = 1;
        uint32_t a = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(n)));
        net_log_hex("NET.dhcp res=ok ip", a);
        net_log_hex("NET.dhcp gw", lwip_ntohl(ip4_addr_get_u32(netif_ip4_gw(n))));
    } else got_ip = 0;
    nethost_note(was && !got_ip);
}
static void a4(ip4_addr_t *a, const uint8_t v[4]) { IP4_ADDR(a, v[0], v[1], v[2], v[3]); }
static void apply_cfg(void)               /* 링크가 있을 때: 수동이면 주소를 넣고, 아니면 DHCP */
{
    if (!nic || last_link != 1) return;
    if (cfg_manual) {
        ip4_addr_t ip, mk, gw; ip_addr_t d;
        dhcp_stop(&nif);
        a4(&ip, cfg_ip); a4(&mk, cfg_mask); a4(&gw, cfg_gw);
        netif_set_addr(&nif, &ip, &mk, &gw);
        if (cfg_dns[0] | cfg_dns[1] | cfg_dns[2] | cfg_dns[3]) { IP_ADDR4(&d, cfg_dns[0], cfg_dns[1], cfg_dns[2], cfg_dns[3]); dns_setserver(0, &d); }
        net_log("NET.cfg manual");
    } else {
        ip4_addr_t z; ip4_addr_set_zero(&z);
        if (ip4_addr_get_u32(netif_ip4_addr(&nif)) != 0) netif_set_addr(&nif, &z, &z, &z);
        lease_arm(); dhcp_start(&nif); dhcp_t0 = now_ms; net_log("NET.dhcp start");
    }
}
void net_config(int manual, const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4], const uint8_t dns[4])
{
    int ch = manual != cfg_manual || (manual && (memcmp(ip, cfg_ip, 4) || memcmp(mask, cfg_mask, 4) || memcmp(gw, cfg_gw, 4) || memcmp(dns, cfg_dns, 4)));
    cfg_manual = manual ? 1 : 0;
    memcpy(cfg_ip, ip, 4); memcpy(cfg_mask, mask, 4); memcpy(cfg_gw, gw, 4); memcpy(cfg_dns, dns, 4);
    if (ch) apply_cfg();
}
int net_ipinfo(uint8_t out[20])           /* 0 = 성공: IP, 마스크, 게이트웨이, DNS1, DNS2 */
{
    if (!got_ip) return -1;
    uint32_t v[3] = { ip4_addr_get_u32(netif_ip4_addr(&nif)), ip4_addr_get_u32(netif_ip4_netmask(&nif)), ip4_addr_get_u32(netif_ip4_gw(&nif)) };
    memcpy(out, v, 12);                    /* lwIP는 네트워크 바이트 순서로 저장 → 바이트 그대로 a.b.c.d */
    for (int i = 0; i < 2; i++) {
        const ip_addr_t *d = dns_getserver((u8_t)i);
        uint32_t w = d ? ip4_addr_get_u32(ip_2_ip4(d)) : 0;
        memcpy(out + 12 + 4 * i, &w, 4);
    }
    return 0;
}
void net_renew(void) { if (nic && last_link == 1 && !got_ip) apply_cfg(); }   /* 카트리지를 다시 꽂았는데 주소가 없으면 다시 받기 */
int net_state(void)                       /* 0 = 없음/끊김, 1 = 주소 기다림, 2 = 연결됨 */
{
    if (!nic || last_link != 1) return 0;
    return got_ip ? 2 : 1;
}

static int initing;                       /* 1 = 칩 초기화가 아직 진행 중 (net_poll이 나눠서 진행) */
static void finish_init(void)
{
    { uint8_t m6[6] = { 0 }; if (nic && nic->mac) nic->mac(m6); net_seed(m6); }
    if (!lwip_ready) { lwip_init(); lwip_ready = 1; }
    ip4_addr_t z; ip4_addr_set_zero(&z);
    netif_add(&nif, &z, &z, &z, 0, if_init, ethernet_input);
    netif_set_hostname(&nif, "X86MSX");
    netif_set_default(&nif);
    netif_set_status_callback(&nif, st_cb);
    netif_set_up(&nif);
    net_log("NET.init res=ok");
}

static int want_kind;                      /* 0 = 아무거나, NK_WIRED / NK_WIFI = 그 종류의 랜카드만 시험 (유선·무선을 사용자가 고름) */
void net_set_kind(int k) { want_kind = k; }
void net_start(uint32_t ms)
{
    if (started) return;
    started = 1; now_ms = ms;
    net_log("NET.init start");
    for (int i = 0; i < nicdb_count() && !nic; i++) {      /* 랜카드 표(nicdb.c)에서 드라이버가 있는 줄을 차례로 시험 */
        const NicId *e = nicdb_at(i);
        if (!e->drv) continue;
        if (want_kind && e->kind != want_kind) continue;
        int dup = 0; for (int j = 0; j < i; j++) if (nicdb_at(j)->drv == e->drv) dup = 1;
        if (!dup && e->drv->probe()) nic = e->drv;
    }
    if (!nic) { net_log("NET.init res=skip why=no_supported_nic"); return; }
    if (nic->step) { initing = 1; net_log("NET.init pending"); return; }     /* 칩 초기화는 net_poll이 프레임마다 조금씩 */
    finish_init();
}

static uint32_t poll_cnt;
void net_poll(uint32_t ms)
{
    now_ms = ms; poll_cnt++;
    if (!nic) return;
    if (initing) {
        int r = nic->step();
        if (r == 0) return;
        initing = 0;
        if (r < 0) { net_log("NET.init res=fail"); nic = 0; return; }
        finish_init();
    }
    if (nic->poll) nic->poll();                       /* 무선 접속 유지·재접속 */
    int lk = nic->link();
    if (lk != last_link) {
        last_link = lk;
        net_log(lk ? "NET.link up" : "NET.link down");
        if (nic->diag) { nic->diag(); diag_next = ms + 4000; diag_left = lk ? 0 : 5; }   /* 링크가 없으면 3초마다 5번 더 상태를 남긴다 */
        if (lk) {
            struct dhcp *dh0 = netif_dhcp_data(&nif);
            /* 링크 올림이 DHCP를 한 번 더 시작(새 xid)하고 apply_cfg가 또 시작해 먼저 온 OFFER가 버려졌다 → 새로 받을 때는 먼저 끄고 한 번만 시작 */
            if (!(!cfg_manual && dh0 && dh0->state != 0 && ip4_addr_get_u32(netif_ip4_addr(&nif)) != 0)) dhcp_stop(&nif);
            netif_set_link_up(&nif);
            dh0 = netif_dhcp_data(&nif);
            if (!cfg_manual && dh0 && dh0->state != 0 && ip4_addr_get_u32(netif_ip4_addr(&nif)) != 0) {
                /* 윈도우처럼 — 링크가 돌아오면 마지막으로 받은 주소를 먼저 다시 요청(lwIP가 REBOOTING으로 확인) 하고, 그동안은 그 주소를 계속 쓴다 */
                dhcp_t0 = now_ms; net_log("NET.dhcp reuse"); st_cb(&nif);
            } else apply_cfg();
        }
        else { netif_set_link_down(&nif); if (got_ip) { got_ip = 0; nethost_note(1); } }
    }
    if (nic->diag && diag_left && (int32_t)(ms - diag_next) >= 0) { diag_next = ms + 3000; diag_left--; net_log("NET.diag"); nic->diag(); }
    for (int i = 0; i < 8; i++) {
        int n = nic->rx(fbuf, sizeof fbuf);
        if (n <= 0) break;
        rx_cnt++;
        struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)n, PBUF_POOL);
        if (!p) { net_log("NET.rx drop why=nopbuf"); break; }
        pbuf_take(p, fbuf, (u16_t)n);
        if (nif.input(p, &nif) != ERR_OK) pbuf_free(p);
    }
    sys_check_timeouts();
    if (!got_ip && last_link == 1 && !cfg_manual && dhcp_t0 && (int32_t)(ms - dhcp_t0) >= 60000) {    /* 1분 넘게 주소가 없으면 처음부터 다시 (윈도우는 계속 재시도) */
        dhcp_t0 = ms; memset(lease_ip, 0, 4); ip4_addr_set_zero(&x86msx_dhcp_reboot_ip); dhcp_stop(&nif); dhcp_start(&nif); net_log("NET.dhcp restart");
    }
    if (!got_ip && last_link == 1 && wait_left && (int32_t)(ms - wait_next) >= 0) {     /* 주소를 못 받는 동안 5초마다 상태를 기록 */
        char b[96]; struct dhcp *dh = netif_dhcp_data(&nif);
        wait_next = ms + 5000; wait_left--;
        b[0] = 0; int n = 0; const char *k = "NET.wait dhcp=";
        while (*k) b[n++] = *k++;
        { int st_ = dh ? dh->state : 99; if (st_ >= 10) { b[n++] = (char)('0' + st_ / 10 % 10); } b[n++] = (char)('0' + st_ % 10); } b[n++] = ' '; b[n++] = 't'; b[n++] = 'x'; b[n++] = '=';
        { uint32_t v = tx_ok; char r[11]; int m = 0; do { r[m++] = (char)('0' + v % 10); v /= 10; } while (v); while (m) b[n++] = r[--m]; }
        b[n++] = ' '; b[n++] = 'r'; b[n++] = 'x'; b[n++] = '=';
        { uint32_t v = rx_cnt; char r[11]; int m = 0; do { r[m++] = (char)('0' + v % 10); v /= 10; } while (v); while (m) b[n++] = r[--m]; }
        b[n++] = ' '; b[n++] = 'p'; b[n++] = 'o'; b[n++] = 'l'; b[n++] = 'l'; b[n++] = '=';   /* 이 줄 사이에 net_poll 이 몇 번 불렸나 (프레임이 느리면 타이머가 늦어짐) */
        { uint32_t v = poll_cnt; char r[11]; int m = 0; do { r[m++] = (char)('0' + v % 10); v /= 10; } while (v); while (m) b[n++] = r[--m]; }
        b[n] = 0; net_log(b);
    }
}
#ifndef FE_UEFI
extern void nic_bcm57785_halt(void);
#endif
void net_halt(void)                       /* 재부팅·전원 끄기 직전 (카드를 이미 뺀 뒤에도 칩이 켜져 있으면 끈다) */
{
    if (nic) net_stop();
#ifndef FE_UEFI
    nic_bcm57785_halt();
#endif
}
int net_has_nic(void) { return nic != 0; }
int net_hw_present(void)                  /* 랜카드가 PCI에 있는지만 (읽기만 한다. 드라이버·lwIP는 건드리지 않음) → 옵션 창에 유선 항목을 보이기 위해 */
{
    static int c = -1; int b, d, f;
    if (c < 0) c = nic_pci_find(0x14E4, 0x16B5, &b, &d, &f) ? 1 : 0;
    return c;
}
void net_stop(void)                       /* 끄기·재시작 직전, 또는 카트리지를 슬롯에서 뺐을 때: 랜카드 DMA를 멈추고 스택을 내린다 (다시 꽂으면 net_start부터) */
{
    if (!nic) return;
    if (initing) {                                                          /* 초기화 도중에 뺐다: 끝까지 마친 뒤(최대 3초) 칩만 정리, 스택은 아직 안 올라옴 */
        for (int i = 0; i < 3000; i++) { int r = nic->step(); if (r) break; now_ms++; nic_udelay(1000); }
        initing = 0;
        if (nic->stop) { nic->stop(); net_log("NET.stop"); }
        nic = 0; got_ip = 0; last_link = -1; started = 0;
        return;
    }
    nethost_note(1);
    dhcp_stop(&nif); netif_set_down(&nif); netif_remove(&nif);
    if (nic->stop) { nic->stop(); net_log("NET.stop"); }
    nic = 0; got_ip = 0; last_link = -1; started = 0;
}
int net_online(void) { return got_ip; }
int atoi(const char *s) { int v = 0, ng = 0; if (*s == '-') { ng = 1; s++; } while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return ng ? -v : v; }

void net_diag_text(char *o, int max)      /* 옵션 창 진단 줄 */
{
    static const char *const ds[] = { "off", "requesting", "init", "rebooting", "rebinding", "renewing", "selecting", "informing", "checking", "permanent", "bound", "releasing", "backoff", "?" };
    struct dhcp *dh = nic ? netif_dhcp_data(&nif) : 0;
    int n = 0; const char *p = nic ? (got_ip ? "lwIP ok" : "lwIP no address") : "(no NIC)";
    while (*p && n < max - 1) o[n++] = *p++;
    if (nic) {
        char t[64]; int m = 0; const char *q = last_link == 1 ? " link=up" : " link=down";
        while (*q) t[m++] = *q++;
        q = " dhcp="; while (*q) t[m++] = *q++;
        q = ds[dh && dh->state < 13 ? dh->state : 13]; while (*q) t[m++] = *q++;
        q = " tx="; while (*q) t[m++] = *q++;
        { uint32_t v = tx_ok; char r[11]; int k = 0; do { r[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) t[m++] = r[--k]; }
        q = " rx="; while (*q) t[m++] = *q++;
        { uint32_t v = rx_cnt; char r[11]; int k = 0; do { r[k++] = (char)('0' + v % 10); v /= 10; } while (v); while (k) t[m++] = r[--k]; }
        for (int i = 0; i < m && n < max - 1; i++) o[n++] = t[i];
    }
    o[n] = 0;
}
