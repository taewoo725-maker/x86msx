/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* usbnic_asix.c - 5.2.117 USB 유선 랜 드라이버: ASIX AX88772/772A/772B (USB2 100M) 와 AX88179/178A (USB3 기가). **미시험** (장치 없음)
 * 레지스터 순서·값은 U-Boot drivers/usb/eth/asix.c · asix88179.c (GPL-2.0) 를 참고해 UEFI USB_IO 로 다시 씀 (THIRD-PARTY-NOTICES.txt).
 * 제조사 전용 인터페이스(클래스 FF)를 쓰는 장치만 맡는다 (펌웨어는 첫 구성만 열어 준다).
 * 받기: 빈 끝점에서 bulk 읽기의 시간 제한 처리가 느려(약 200ms) 일정 간격(25ms)으로만 읽는다 → 속도는 느릴 수 있음. 모든 단계는 net_log 로 남긴다 (NET.usb asix ...) */
#include "efi.h"
#include "../kernel/net/nic.h"
#include "../kernel/net/nicdb.h"
#include <string.h>

#define EFI_TO (0x8000000000000000ull | 18)
static EFI_BOOT_SERVICES *UBS;
void usbnic_asix_init(EFI_BOOT_SERVICES *bs) { UBS = bs; }

static EFI_USB_IO_PROTOCOL *io;
static uint8_t ep_in, ep_out; static uint16_t mps_out, vid, pid;
static int is179, super_speed, phase, nerr, link_up, tx_n, tx_bad, rx_n, rx_bad, phy_id, was_started;
static uint32_t link_next, rx_next;
static uint8_t macaddr[6];
static uint8_t cbuf[64] __attribute__((aligned(64)));
static uint8_t txb[2048] __attribute__((aligned(64)));
#define RX_BUF 24576
static uint8_t rxb[RX_BUF] __attribute__((aligned(64)));
static int rx_len, rx_pos, rx_cnt, rx_hdr;   /* 179: rx_cnt = 남은 패킷 수, rx_hdr = 패킷 머리 위치 */

static void ulog(const char *k, uint32_t v) { net_log_hex(k, v); }
static void mdelay(int ms) { if (UBS) UBS->Stall((UINTN)ms * 1000); }

/* 제조사 전용 제어 요청 (bmRequestType 0x40 쓰기 / 0xC0 읽기) */
static int vreq(int wr, uint8_t cmd, uint16_t value, uint16_t index, void *data, uint16_t n)
{
    EFI_USB_DEVICE_REQUEST rq; uint32_t st = 0;
    rq.RequestType = wr ? 0x40 : 0xC0; rq.Request = cmd; rq.Value = value; rq.Index = index; rq.Length = n;
    if (wr && n) memcpy(cbuf, data, n);
    EFI_STATUS s = io->UsbControlTransfer(io, &rq, n ? (wr ? EfiUsbDataOut : EfiUsbDataIn) : EfiUsbNoData, 1000, n ? cbuf : 0, n, &st);
    if (s != EFI_SUCCESS) { if (nerr < 6) { ulog("NET.usb asix ctl_err cmd", cmd); ulog("NET.usb asix ctl_err val", value); ulog("NET.usb asix ctl_err st", (uint32_t)(s & 0xFFFF)); } nerr++; return -1; }
    if (!wr && n) memcpy(data, cbuf, n);
    return 0;
}
static void clear_halt(uint8_t ep)
{
    EFI_USB_DEVICE_REQUEST rq; uint32_t st = 0;
    rq.RequestType = 0x02; rq.Request = 1; rq.Value = 0; rq.Index = ep; rq.Length = 0;
    io->UsbControlTransfer(io, &rq, EfiUsbNoData, 500, 0, 0, &st);
}

/* ---------------- AX88772 ---------------- */
#define A7_SET_SW_MII 0x06
#define A7_READ_MII 0x07
#define A7_WRITE_MII 0x08
#define A7_SET_HW_MII 0x0a
#define A7_READ_EEPROM 0x0b
#define A7_WRITE_RX_CTL 0x10
#define A7_WRITE_IPG0 0x12
#define A7_READ_NODE_ID 0x13
#define A7_WRITE_NODE_ID 0x14
#define A7_READ_PHY_ID 0x19
#define A7_WRITE_MEDIUM 0x1b
#define A7_WRITE_GPIOS 0x1f
#define A7_SW_RESET 0x20
#define A7_SW_PHY_SELECT 0x22
#define A7_MEDIUM_DEFAULT 0x0336            /* FD | RFC | TFC | PS | AC | RE */
static uint16_t a7_mii_r(int phy, int reg)
{
    uint8_t b[2] = { 0, 0 };
    vreq(1, A7_SET_SW_MII, 0, 0, 0, 0);
    vreq(0, A7_READ_MII, (uint16_t)phy, (uint16_t)reg, b, 2);
    vreq(1, A7_SET_HW_MII, 0, 0, 0, 0);
    return (uint16_t)(b[0] | b[1] << 8);
}
static void a7_mii_w(int phy, int reg, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    vreq(1, A7_SET_SW_MII, 0, 0, 0, 0);
    vreq(1, A7_WRITE_MII, (uint16_t)phy, (uint16_t)reg, b, 2);
    vreq(1, A7_SET_HW_MII, 0, 0, 0, 0);
}
static int embd_phy;
static int a7_step(void)
{
    switch (phase) {
    case 0: {
        uint8_t b[2] = { 0, 0 };
        if (vreq(1, A7_WRITE_GPIOS, 0xB0 /* RSE|GPO_2|GPO2EN */, 0, 0, 0)) return -1;
        mdelay(5);
        vreq(0, A7_READ_PHY_ID, 0, 0, b, 2);
        embd_phy = ((b[1] & 0x1f) == 0x10) ? 1 : 0;
        ulog("NET.usb asix phyid", b[1]);
        vreq(1, A7_SW_PHY_SELECT, (uint16_t)embd_phy, 0, 0, 0);
        phase = 1; return 0;
    }
    case 1: vreq(1, A7_SW_RESET, 0x48 /* IPPD|PRL */, 0, 0, 0); mdelay(150); phase = 2; return 0;
    case 2: vreq(1, A7_SW_RESET, 0x00, 0, 0, 0); mdelay(150); phase = 3; return 0;
    case 3: vreq(1, A7_SW_RESET, embd_phy ? 0x20 /* IPRL */ : 0x04 /* PRTE */, 0, 0, 0); mdelay(150); phase = 4; return 0;
    case 4: {
        uint8_t b[2] = { 0, 0 };
        vreq(1, A7_WRITE_RX_CTL, 0, 0, 0, 0);
        vreq(0, A7_READ_PHY_ID, 0, 0, b, 2); phy_id = b[1];
        a7_mii_w(phy_id, 0, 0x8000);                 /* BMCR_RESET */
        mdelay(50);
        a7_mii_w(phy_id, 4, 0x01E1);                 /* ADVERTISE_ALL | CSMA */
        { uint16_t bm = a7_mii_r(phy_id, 0); if (bm & 0x1000) a7_mii_w(phy_id, 0, bm | 0x0200); }   /* 자동 협상 다시 시작 */
        vreq(1, A7_WRITE_MEDIUM, A7_MEDIUM_DEFAULT, 0, 0, 0);
        vreq(1, A7_WRITE_IPG0, 0x15 | 0x0c, 0x12, 0, 0);
        phase = 5; return 0;
    }
    case 5: {
        uint8_t m[6] = { 0 };
        if (pid == 0x772B) {                         /* 772B: MAC 이 EEPROM 04h 부터 */
            for (int i = 0; i < 3; i++) { uint8_t t[2] = { 0, 0 }; vreq(0, A7_READ_EEPROM, (uint16_t)(4 + i), 0, t, 2); m[i * 2] = t[0]; m[i * 2 + 1] = t[1]; }
        } else vreq(0, A7_READ_NODE_ID, 0, 0, m, 6);
        memcpy(macaddr, m, 6);
        ulog("NET.usb asix mac_hi", macaddr[0] << 16 | macaddr[1] << 8 | macaddr[2]);
        ulog("NET.usb asix mac_lo", macaddr[3] << 16 | macaddr[4] << 8 | macaddr[5]);
        vreq(1, A7_WRITE_NODE_ID, 0, 0, macaddr, 6);
        vreq(1, A7_WRITE_RX_CTL, 0x0088 /* SO | AB */, 0, 0, 0);
        clear_halt(ep_in); clear_halt(ep_out);
        phase = 6; return 1;
    }
    }
    return 1;
}

/* ---------------- AX88179 ---------------- */
#define G_MAC 0x01
#define G_PHY 0x02
#define G_PHYID 0x03
static const uint8_t g_bulkin[4][5] = { { 7, 0x4f, 0, 0x12, 0xff }, { 7, 0x20, 3, 0x16, 0xff }, { 7, 0xae, 7, 0x18, 0xff }, { 7, 0xcc, 0x4c, 0x18, 8 } };
static int g_w(uint8_t reg, uint16_t n, const void *d) { return vreq(1, G_MAC, reg, n, (void *)d, n); }
static int g_r(uint8_t reg, uint16_t n, void *d) { return vreq(0, G_MAC, reg, n, d, n); }
static uint16_t g_pr(int reg) { uint8_t b[2] = { 0, 0 }; vreq(0, G_PHY, G_PHYID, (uint16_t)reg, b, 2); return (uint16_t)(b[0] | b[1] << 8); }
static void g_pw(int reg, uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; vreq(1, G_PHY, G_PHYID, (uint16_t)reg, b, 2); }
static void w16(uint8_t reg, uint16_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; g_w(reg, 2, b); }
static void w8(uint8_t reg, uint8_t v) { g_w(reg, 1, &v); }
static int g_step(void)
{
    switch (phase) {
    case 0:
        g_r(0x10, 6, macaddr);                       /* AX_NODE_ID */
        ulog("NET.usb asix mac_hi", macaddr[0] << 16 | macaddr[1] << 8 | macaddr[2]);
        ulog("NET.usb asix mac_lo", macaddr[3] << 16 | macaddr[4] << 8 | macaddr[5]);
        w16(0x26, 0x0000); w16(0x26, 0x0020);        /* PHYPWR_RSTCTL: 0, IPRL */
        mdelay(200); phase = 1; return 0;
    case 1:
        w8(0x33, 0x03);                              /* CLK_SELECT: ACS | BCS */
        mdelay(200); phase = 2; return 0;
    case 2:
        g_w(0x2e, 5, g_bulkin[0]);                   /* RX_BULKIN_QCTRL */
        w8(0x54 + 0, 0x52); w8(0x55, 0x34);          /* 물 높이: HIGH 0x54=0x52, LOW 0x55=0x34 */
        w8(0x34, 0x67);                              /* RXCOE_CTL: IP|TCP|UDP|TCPV6|UDPV6 */
        w8(0x35, 0x67);                              /* TXCOE_CTL */
        w16(0x0b, 0x03AA);                           /* RX_CTL: DROPCRCERR|IPE|START|AP|AMALL|AB */
        w8(0x24, 0x64);                              /* MONITOR_MOD: PMETYPE|PMEPOL|RWMP */
        w16(0x22, 0x8173);                           /* MEDIUM: RE|TXFC|RXFC|FD|GIGA|JUMBO */
        phase = 3; return 0;
    case 3: {
        g_pw(0, 0x8000);                             /* PHY 리셋 */
        for (int i = 0; i < 10; i++) { mdelay(50); if (!(g_pr(0) & 0x8000)) break; }
        g_pw(4, 0x01E1 | 0x0C00);                    /* ADVERTISE_ALL | CSMA | PAUSE */
        g_pw(9, 0x0200);                             /* 1000 FULL */
        g_pw(0, g_pr(0) | 0x1200);                   /* 자동 협상 시작 */
        clear_halt(ep_in); clear_halt(ep_out);
        phase = 4; return 1;
    }
    }
    return 1;
}
static void g_link_config(void)                      /* 링크가 올라오면: 속도에 맞춰 medium·bulk-in 묶음 크기 */
{
    uint8_t sts = 0; uint16_t physr = g_pr(0x11), mode = 0x0130; const uint8_t *q;
    g_r(0x02, 1, &sts);
    if ((physr & 0xc000) == 0x8000) { mode |= 0x0001 | 0x0008 | 0x8040; q = (sts & 4) ? g_bulkin[0] : (sts & 2) ? g_bulkin[1] : g_bulkin[3]; }
    else if ((physr & 0xc000) == 0x4000) { mode |= 0x0200; q = (sts & 6) ? g_bulkin[2] : g_bulkin[3]; }
    else q = g_bulkin[3];
    g_w(0x2e, 5, q);
    if (physr & 0x2000) mode |= 0x0002;              /* 전이중 */
    w16(0x22, mode);
    ulog("NET.usb asix physr", physr); ulog("NET.usb asix usbsts", sts);
}

/* ---------------- Nic ---------------- */
static int do_match(void)
{
    EFI_HANDLE *hs = 0; UINTN n = 0; int found = 0;
    if (!UBS) return 0;
    if (UBS->LocateHandleBuffer(ByProtocol, &USB_IO_GUID, 0, &n, &hs) != EFI_SUCCESS) n = 0;
    for (UINTN j = 0; j < n && !found; j++) {
        EFI_USB_IO_PROTOCOL *u; EFI_USB_DEVICE_DESCRIPTOR dd; EFI_USB_INTERFACE_DESCRIPTOR ifd;
        if (UBS->HandleProtocol(hs[j], &USB_IO_GUID, (void **)&u) != EFI_SUCCESS) continue;
        memset(&dd, 0, sizeof dd);
        if (!u->UsbGetDeviceDescriptor || u->UsbGetDeviceDescriptor(u, &dd) != EFI_SUCCESS) continue;
        if (u->UsbGetInterfaceDescriptor(u, &ifd) != EFI_SUCCESS) continue;
        const NicId *e = nicdb_usb(dd.IdVendor, dd.IdProduct, ifd.InterfaceClass, ifd.InterfaceSubClass);
        if (!e || e->drv != &nic_asix) continue;
        uint8_t in = 0, out = 0; uint16_t mo = 512;
        for (uint8_t i = 0; i < ifd.NumEndpoints; i++) {
            EFI_USB_ENDPOINT_DESCRIPTOR ep;
            if (u->UsbGetEndpointDescriptor(u, i, &ep) != EFI_SUCCESS) continue;
            if ((ep.Attributes & 3) != 2) continue;
            if (ep.EndpointAddress & 0x80) { if (!in) in = ep.EndpointAddress; } else if (!out) { out = ep.EndpointAddress; mo = ep.MaxPacketSize & 0x7ff; }
        }
        ulog("NET.usb asix vid", dd.IdVendor); ulog("NET.usb asix pid", dd.IdProduct);
        ulog("NET.usb asix ep_in", in); ulog("NET.usb asix ep_out", out); ulog("NET.usb asix bcdUSB", dd.BcdUSB);
        if (!in || !out) { net_log("NET.usb asix skip why=no_bulk_endpoints"); continue; }
        io = u; ep_in = in; ep_out = out; mps_out = mo ? mo : 512; vid = dd.IdVendor; pid = dd.IdProduct;
        super_speed = dd.BcdUSB >= 0x0300;
        is179 = (pid == 0x1790 || pid == 0x178A || pid == 0x4A00 || pid == 0x0072 || pid == 0xA100 || pid == 0x304B || pid == 0x3610);
        found = 1;
    }
    if (hs) UBS->FreePool(hs);
    return found;
}
static int probe(void)
{
    if (!do_match()) { net_log("NET.usb asix res=none"); return 0; }
    phase = 0; nerr = 0; link_up = 0; link_next = rx_next = 0; rx_len = rx_pos = rx_cnt = 0; tx_n = tx_bad = rx_n = rx_bad = 0;
    if (was_started && io->UsbPortReset) {
        EFI_STATUS pr = ((EFI_STATUS (*)(struct EFI_USB_IO_PROTOCOL *))io->UsbPortReset)(io);
        ulog("NET.usb asix port_reset res", (uint32_t)(pr & 0xFFFF)); mdelay(300);
    }
    was_started = 1;
    net_log(is179 ? "NET.usb asix chip=AX88179" : "NET.usb asix chip=AX88772");
    net_log("NET.usb asix res=found");
    return 1;
}
static int step(void)
{
    int r = is179 ? g_step() : a7_step();
    if (r > 0) { net_log("NET.usb asix step=enabled"); ulog("NET.usb asix ctl_errors", (uint32_t)nerr); }
    if (nerr > 20) return -1;
    return r;
}
static void mac(uint8_t o[6]) { memcpy(o, macaddr, 6); }
static int link(void)
{
    uint32_t now = net_now();
    if (phase >= (is179 ? 4 : 6) && (int32_t)(now - link_next) >= 0) {
        link_next = now + 500;
        uint16_t bm;
        if (is179) { g_pr(1); bm = g_pr(1); } else { a7_mii_r(phy_id, 1); bm = a7_mii_r(phy_id, 1); }
        int l = (bm & 0x0004) != 0;
        if (l != link_up) { link_up = l; ulog("NET.usb asix bmsr", bm); if (l && is179) g_link_config(); }
    }
    return link_up;
}
static int tx(const uint8_t *f, int n)
{
    if (n <= 0 || n > 1700) return -1;
    int hl;
    if (is179) {
        uint32_t h2 = (((n + 8) % mps_out) == 0) ? 0x80008000u : 0;
        txb[0] = (uint8_t)n; txb[1] = (uint8_t)(n >> 8); txb[2] = txb[3] = 0;
        txb[4] = (uint8_t)h2; txb[5] = (uint8_t)(h2 >> 8); txb[6] = (uint8_t)(h2 >> 16); txb[7] = (uint8_t)(h2 >> 24);
        hl = 8;
    } else {
        uint32_t h = ((uint32_t)(n ^ 0xffff) << 16) + (uint32_t)n;
        txb[0] = (uint8_t)h; txb[1] = (uint8_t)(h >> 8); txb[2] = (uint8_t)(h >> 16); txb[3] = (uint8_t)(h >> 24);
        hl = 4;
    }
    memcpy(txb + hl, f, (size_t)n);
    UINTN l = (UINTN)(n + hl); uint32_t st = 0;
    if (!is179 && (l % mps_out) == 0) { txb[l] = 0; txb[l + 1] = 0; txb[l + 2] = 0xff; txb[l + 3] = 0xff; l += 4; }   /* 772: 짧은 패킷으로 끝내려고 4바이트 덧붙임 (리눅스와 같음) */
    EFI_STATUS s = io->UsbBulkTransfer(io, ep_out, txb, &l, 200, &st);
    if (s != EFI_SUCCESS) { if (tx_bad < 4) { ulog("NET.usb asix tx_err", (uint32_t)(s & 0xFFFF)); ulog("NET.usb asix tx_err st", st); } tx_bad++; return -1; }
    tx_n++; return 0;
}
static int rx_fill(void)
{
    uint32_t now = net_now();
    if ((int32_t)(now - rx_next) < 0) return 0;      /* 빈 끝점 읽기가 느리므로 25ms 간격 */
    rx_next = now + 25;
    UINTN l = RX_BUF; uint32_t st = 0;
    EFI_STATUS s = io->UsbBulkTransfer(io, ep_in, rxb, &l, 1, &st);
    if (s != EFI_SUCCESS || l < 4) { if (s != EFI_SUCCESS && s != EFI_TO && rx_bad < 4) { ulog("NET.usb asix rx_err", (uint32_t)(s & 0xFFFF)); ulog("NET.usb asix rx_err st", st); rx_bad++; } return 0; }
    rx_len = (int)l; rx_pos = 0; rx_cnt = 0;
    if (is179) {
        uint32_t h = rxb[l - 4] | rxb[l - 3] << 8 | rxb[l - 2] << 16 | (uint32_t)rxb[l - 1] << 24;
        rx_cnt = (int)(h & 0xffff); rx_hdr = (int)(h >> 16);
        if (!rx_cnt || rx_hdr > (int)l - 4) { rx_len = rx_cnt = 0; return 0; }
    }
    return 1;
}
static int rx(uint8_t *f, int max)
{
    if (is179) {
        if (rx_cnt <= 0) { rx_len = rx_pos = 0; if (!rx_fill()) return 0; }
        if (rx_hdr + 4 > rx_len) { rx_cnt = 0; return 0; }
        uint32_t ph = rxb[rx_hdr] | rxb[rx_hdr + 1] << 8 | rxb[rx_hdr + 2] << 16 | (uint32_t)rxb[rx_hdr + 3] << 24;
        int pl = (int)((ph >> 16) & 0x1fff);
        if (rx_pos + 2 + pl > rx_len || pl < 14 || (ph & 0xA0000000u)) {              /* CRC 오류·버림 표시 */
            rx_hdr += 4; rx_pos += (pl + 7) & ~7; rx_cnt--; rx_bad++; return 0;
        }
        int r = pl > max ? max : pl;
        memcpy(f, rxb + rx_pos + 2, (size_t)r);
        rx_hdr += 4; rx_pos += (pl + 7) & ~7; rx_cnt--; rx_n++;
        return r;
    }
    if (rx_pos >= rx_len) { rx_pos = rx_len = 0; if (!rx_fill()) return 0; }
    if (rx_pos + 4 > rx_len) { rx_pos = rx_len = 0; return 0; }
    uint32_t h = rxb[rx_pos] | rxb[rx_pos + 1] << 8 | rxb[rx_pos + 2] << 16 | (uint32_t)rxb[rx_pos + 3] << 24;
    if ((((~h) >> 16) & 0x7ff) != (h & 0x7ff)) { rx_pos = rx_len = 0; rx_bad++; return 0; }
    int pl = (int)(h & 0x7ff);
    if (pl < 14 || rx_pos + 4 + pl > rx_len) { rx_pos = rx_len = 0; rx_bad++; return 0; }
    int r = pl > max ? max : pl;
    memcpy(f, rxb + rx_pos + 4, (size_t)r);
    rx_pos += 4 + ((pl + 1) & ~1); rx_n++;
    return r;
}
static void diag(void)
{
    ulog("NET.usb asix tx_ok", (uint32_t)tx_n); ulog("NET.usb asix tx_bad", (uint32_t)tx_bad);
    ulog("NET.usb asix rx_ok", (uint32_t)rx_n); ulog("NET.usb asix rx_bad", (uint32_t)rx_bad);
    ulog("NET.usb asix ctl_errors", (uint32_t)nerr);
    if (is179) { ulog("NET.usb asix bmsr", g_pr(1)); ulog("NET.usb asix physr", g_pr(0x11)); }
    else ulog("NET.usb asix bmsr", a7_mii_r(phy_id, 1));
}
static void stop(void)
{
    if (!io || phase < 4) return;
    if (is179) { w16(0x0b, 0); w16(0x22, 0); } else { vreq(1, A7_WRITE_RX_CTL, 0, 0, 0, 0); }
    io = 0; phase = 0;
}
const Nic nic_asix = { .name = "ASIX AX88772/AX88179", .probe = probe, .mac = mac, .link = link, .tx = tx, .rx = rx, .diag = diag, .stop = stop, .step = step, .mtu = 0 };
