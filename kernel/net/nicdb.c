/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* nicdb.c - 5.2.101 랜카드 표 (설명 nicdb.h). 새 랜카드: 드라이버 파일 하나 + 아래 표에 한 줄. */
#include "nicdb.h"

#ifdef FE_UEFI
#define DRV_RTW     (&nic_rtw)        /* uefi/rtwdet.c */
#define DRV_R8152   (&nic_r8152)      /* uefi/usbnic_r8152.c */
#define DRV_ASIX    (&nic_asix)       /* uefi/usbnic_asix.c (5.2.117, 미시험) */
#define DRV_BCM5778 0                 /* 레거시(BIOS) 판에만 있음 */
#else
#define DRV_R8152   0
#define DRV_ASIX    0
#define DRV_RTW     0                 /* UEFI 판에만 있음 */
#define DRV_BCM5778 (&nic_bcm57785)   /* kernel/net/nic_bcm57785.c */
#endif

static const NicId tab[] = {
    /* 붙는 길  종류      제조사   장치      클래스  칩 이름                          드라이버 */
    /* ---- PCI 무선 ---- */
    { NB_PCI, NK_WIFI,  0x10EC, 0xC822,   0,      "Realtek RTL8822CE",              DRV_RTW },
    /* ---- PCI 유선 ---- */
    { NB_PCI, NK_WIRED, 0x14E4, 0x16B5,   0,      "Broadcom BCM57785",              DRV_BCM5778 },
    /* ---- USB 유선 (제조사 전용 인터페이스 FF/00) ---- */
    { NB_USB, NK_WIRED, 0x0BDA, 0x8050,   0,      "Realtek RTL8152/8153",           DRV_R8152 },
    { NB_USB, NK_WIRED, 0x0BDA, 0x8152,   0,      "Realtek RTL8152",                DRV_R8152 },
    { NB_USB, NK_WIRED, 0x0BDA, 0x8153,   0,      "Realtek RTL8153",                DRV_R8152 },
    { NB_USB, NK_WIRED, 0x0BDA, 0x8155,   0,      "Realtek RTL8155",                0 },
    { NB_USB, NK_WIRED, 0x0BDA, 0x8156,   0,      "Realtek RTL8156",                0 },
    { NB_USB, NK_WIRED, 0x2357, 0x0601,   0,      "TP-Link UE300 (RTL8153)",        DRV_R8152 },
    { NB_USB, NK_WIRED, 0x2357, 0x0602,   0,      "TP-Link UE200 (RTL8152)",        DRV_R8152 },
    { NB_USB, NK_WIRED, 0x17EF, 0x3069,   0,      "Lenovo (RTL8153)",               DRV_R8152 },
    { NB_USB, NK_WIRED, 0x17EF, 0x3062,   0,      "Lenovo (RTL8153)",               DRV_R8152 },
    { NB_USB, NK_WIRED, 0x17EF, 0x720C,   0,      "Lenovo (RTL8153)",               DRV_R8152 },
    { NB_USB, NK_WIRED, 0x17EF, 0x7205,   0,      "Lenovo (RTL8153)",               DRV_R8152 },
    { NB_USB, NK_WIRED, 0x17EF, 0xA359,   0,      "Lenovo (RTL8153)",               DRV_R8152 },
    { NB_USB, NK_WIRED, 0x17EF, 0xA387,   0,      "Lenovo (RTL8153)",               DRV_R8152 },
    { NB_USB, NK_WIRED, 0x17EF, 0x304F,   0,      "Lenovo (RTL8153)",               DRV_R8152 },
    { NB_USB, NK_WIRED, 0x04E8, 0xA101,   0,      "Samsung (RTL8153)",              DRV_R8152 },
    { NB_USB, NK_WIRED, 0x0B95, 0x772A,   0,      "ASIX AX88772A",                  DRV_ASIX },
    { NB_USB, NK_WIRED, 0x0B95, 0x772B,   0,      "ASIX AX88772B",                  DRV_ASIX },
    { NB_USB, NK_WIRED, 0x0B95, 0x7720,   0,      "ASIX AX88772",                   DRV_ASIX },
    { NB_USB, NK_WIRED, 0x0B95, 0x7E2B,   0,      "ASIX AX88772B",                  DRV_ASIX },
    { NB_USB, NK_WIRED, 0x0B95, 0x1790,   0,      "ASIX AX88179 (USB3 기가)",       DRV_ASIX },
    { NB_USB, NK_WIRED, 0x0B95, 0x178A,   0,      "ASIX AX88178A",                  DRV_ASIX },
    { NB_USB, NK_WIRED, 0x2001, 0x4A00,   0,      "D-Link DUB-1312 (AX88179)",      DRV_ASIX },
    { NB_USB, NK_WIRED, 0x0DF6, 0x0072,   0,      "Sitecom (AX88179)",              DRV_ASIX },
    { NB_USB, NK_WIRED, 0x04E8, 0xA100,   0,      "Samsung (AX88179)",              DRV_ASIX },
    { NB_USB, NK_WIRED, 0x17EF, 0x304B,   0,      "Lenovo (AX88179)",               DRV_ASIX },
    { NB_USB, NK_WIRED, 0x04B4, 0x3610,   0,      "Cypress GX3 (AX88179)",          DRV_ASIX },
    { NB_USB, NK_WIRED, 0x05AC, 0x1402,   0,      "Apple USB Ethernet (AX88772)",   DRV_ASIX },
    { NB_USB, NK_WIRED, 0x2001, 0x1A02,   0,      "D-Link DUB-E100 C1 (AX88772)",   DRV_ASIX },
    { NB_USB, NK_WIRED, 0x2001, 0x3C05,   0,      "D-Link DUB-E100 (AX88772)",      DRV_ASIX },
    { NB_USB, NK_WIRED, 0x07D1, 0x3C05,   0,      "D-Link DUB-E100 (AX88772)",      DRV_ASIX },
    { NB_USB, NK_WIRED, 0x0DB0, 0xA877,   0,      "MSI (AX88772A)",                 DRV_ASIX },
    { NB_USB, NK_WIRED, 0x1557, 0x7720,   0,      "0Q0 (AX88772)",                  DRV_ASIX },
    { NB_USB, NK_WIRED, 0x0B95, NID_ANY,  0,      "ASIX AX88xxx (그 밖)",           0 },
    { NB_USB, NK_WIRED, 0x0424, 0x7800,   0,      "Microchip LAN7800",              0 },
    { NB_USB, NK_WIRED, 0x0424, 0x7850,   0,      "Microchip LAN7850",              0 },
    { NB_USB, NK_WIRED, 0x0424, 0xEC00,   0,      "SMSC LAN9512/9514",              0 },
    { NB_USB, NK_WIRED, 0x0424, 0x9500,   0,      "SMSC LAN9500",                   0 },
    { NB_USB, NK_WIRED, 0x0424, 0x9E00,   0,      "SMSC LAN9500A",                  0 },
    { NB_USB, NK_WIRED, 0x0424, 0x9E08,   0,      "SMSC LAN89530",                  0 },
    { NB_USB, NK_WIRED, 0x0846, 0x1040,   0,      "NETGEAR FA120",                  0 },
    { NB_USB, NK_WIRED, 0x0846, 0x9001,   0,      "NETGEAR",                        0 },
    { NB_USB, NK_WIRED, 0x13B1, 0x0041,   0,      "Linksys USB3GIGV1",              0 },
    { NB_USB, NK_WIRED, 0x050D, 0x5055,   0,      "Belkin F5D5055",                 0 },
    /* ---- 규격 클래스 (위 ID에 없는 칩). 맨 끝에 둔다 ---- */
    { NB_USB, NK_WIRED, NID_ANY, NID_ANY, 0x0206, "USB CDC-ECM 이더넷 (규격)",      0 },
    { NB_USB, NK_WIRED, NID_ANY, NID_ANY, 0x020D, "USB CDC-NCM 이더넷 (규격)",      0 },
    { NB_PCI, NK_WIFI,  NID_ANY, NID_ANY, 0x0280, "PCI 무선 랜카드",                0 },
    { NB_PCI, NK_WIRED, NID_ANY, NID_ANY, 0x0200, "PCI 이더넷 랜카드",              0 },
};
#define TAB_N ((int)(sizeof tab / sizeof tab[0]))

int nicdb_count(void) { return TAB_N; }
const NicId *nicdb_at(int i) { return i >= 0 && i < TAB_N ? &tab[i] : 0; }

static int id_ok(const NicId *e, uint16_t v, uint16_t d)
{
    return !e->cls && e->ven == v && (e->dev == NID_ANY || e->dev == d);
}

const NicId *nicdb_pci(uint16_t ven, uint16_t dev, uint32_t cls16)
{
    for (int i = 0; i < TAB_N; i++) if (tab[i].bus == NB_PCI && id_ok(&tab[i], ven, dev)) return &tab[i];
    for (int i = 0; i < TAB_N; i++) if (tab[i].bus == NB_PCI && tab[i].cls && tab[i].cls == cls16) return &tab[i];
    return 0;
}

const NicId *nicdb_usb(uint16_t vid, uint16_t pid, uint8_t cls, uint8_t sub)
{
    uint32_t c = (uint32_t)cls << 8 | sub;
    for (int i = 0; i < TAB_N; i++) if (tab[i].bus == NB_USB && tab[i].cls && tab[i].cls == c) return &tab[i];   /* 규격 인터페이스가 먼저 (표준 드라이버로 쓸 수 있음) */
    if (cls != 0xFF && cls != 0x00) return 0;                                                                     /* ID 줄은 제조사 전용 인터페이스에만 */
    for (int i = 0; i < TAB_N; i++) if (tab[i].bus == NB_USB && id_ok(&tab[i], vid, pid)) return &tab[i];
    return 0;
}
