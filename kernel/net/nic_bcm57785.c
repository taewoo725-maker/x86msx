/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* nic_bcm57785.c - Broadcom NetXtreme BCM57785 (14E4:16B5) 유선 랜카드. 5번 Acer Aspire V5-171 전용.
 * 5.0.94 1단계(관찰) 결과: 레지스터 접근·MAC·PHY(주소1, 1000M 전이중 링크) 정상.
 * 5.0.95 2단계: 칩 리셋 → 링(호스트 메모리) 설정 → 송수신. 모든 단계는 "step=" 줄로 먼저 기록(멈춰도 마지막 단계가 남는다).
 * 칩 레지스터 오프셋은 공개 데이터북 목차와 5.0.94 실제 읽기 결과를 바탕으로 직접 작성했다. 외부 소스를 복사하지 않았다. */
#include "nic.h"
#include "../memmap.h"
#include <string.h>
extern void paging_set_uc(uintptr_t addr);
static volatile uint32_t *mm;
static int B, D, F;
static uint8_t macaddr[6];
static int linkup, ready, suspended, inited, halted, bring_busy;
static uint32_t r(uint32_t o) { return mm[o >> 2]; }
static void w(uint32_t o, uint32_t v) { mm[o >> 2] = v; (void)mm[o >> 2]; }
static void wn(uint32_t o, uint32_t v) { mm[o >> 2] = v; }                 /* 읽기 없이 (메일박스) */
/* 메일박스: 고속 영역(0x200~)과 GRC 영역(0x5800~) 둘 다에 쓴다 (같은 레지스터의 두 주소) */
static void mbox(uint32_t hp, uint32_t grc, uint32_t v) { wn(hp + 4, v); wn(grc + 4, v); }
#define MB_STD(v)  mbox(0x268, 0x5868, (v))
#define MB_RET(v)  mbox(0x280, 0x5880, (v))
#define MB_TX(v)   mbox(0x300, 0x5900, (v))
static uint32_t n_tx, n_txfull, n_rx, n_rxbad;
static void hx(const char *k, uint32_t v) { net_log_hex(k, v); }
/* NIC 내부 SRAM 창 (MMIO 0x7C 기준 주소, 0x84 자료) */
static uint32_t sram_rd(uint32_t a) { w(0x7C, a); uint32_t v = r(0x84); w(0x7C, 0); return v; }
static void sram_wr(uint32_t a, uint32_t v) { w(0x7C, a); w(0x84, v); w(0x7C, 0); }

#define PHY 1
static int mi_rd(int reg, uint32_t *out)
{
    w(0x44C, 0x20000000u | 0x08000000u | ((uint32_t)PHY << 21) | ((uint32_t)reg << 16));
    for (int i = 0; i < 2000; i++) {
        nic_udelay(10);
        uint32_t v = r(0x44C);
        if (!(v & 0x20000000u)) { if (v & 0x10000000u) return -1; *out = v & 0xFFFF; return 0; }
    }
    return -2;
}
static int mi_wr(int reg, uint32_t val)
{
    w(0x44C, 0x20000000u | 0x04000000u | ((uint32_t)PHY << 21) | ((uint32_t)reg << 16) | (val & 0xFFFF));
    for (int i = 0; i < 2000; i++) { nic_udelay(10); if (!(r(0x44C) & 0x20000000u)) return 0; }
    return -2;
}

/* ---- DMA 메모리 (UC 4 MiB 쪽 안, 오디오와 겹치지 않는 +0x200000부터) ---- */
/* MAC 모드에 송신 데이터 엔진(TDE 0x200000)·수신(RDE 0x400000)·프레임헤더(FHDE 0x800000)·통계(0x800/0x2000) 켜기 — 빠져 있어 칩이 호스트 메모리를 안 읽었을 가능성 */
/* GRC_MODE = 자료 워드교환 0x20 + 바이트교환 0x10 + 설명자 워드교환 0x04 + 호스트 송신BD 0x20000 + 송신 의사헤더 체크섬 끔 0x100000 + 호스트스택 0x10000 + MAC 주의 인터럽트 0x4000000.
 * 5.0.102까지는 교환 비트가 없어 칩이 설명자의 주소 위/아래 절반을 거꾸로 읽었을 가능성(→ 잘못된 주소를 읽다 마스터 중단). */
#define GRCMODE 0x04130034u
#define MACEN 0x00E02800u
static uint32_t dmab;
#define DMA      dmab
#define SB       ((volatile uint16_t *)(uintptr_t)(DMA + 0x0000))      /* 상태 블록 */
#define STDRING  ((volatile uint32_t *)(uintptr_t)(DMA + 0x1000))      /* 512 x 32B */
#define RETRING  ((volatile uint32_t *)(uintptr_t)(DMA + 0x5000))      /* 512 x 32B */
#define TXRING   ((volatile uint32_t *)(uintptr_t)(DMA + 0x9000))      /* 512 x 16B */
#define RXBUF(i) ((uint8_t *)(uintptr_t)(DMA + 0x10000 + (i) * 2048))
#define TXBUF(i) ((uint8_t *)(uintptr_t)(DMA + 0x30000 + (i) * 2048))
#define NRXB 32
#define NTXB 16
static uint16_t slotbuf[512];
static uint32_t std_prod, ret_cons, tx_prod, tx_cons, tx_soft;
static int rx_poll_mode;
static int pcie_cap;
static int tx(const uint8_t *f, int n);

static void post_rx(int buf)
{
    uint32_t s = std_prod;
    volatile uint32_t *d = STDRING + s * 8;
    d[0] = 0; d[1] = (uint32_t)(uintptr_t)RXBUF(buf);
    d[2] = 1536u;                            /* 길이 (낮은 16비트; 위 16비트는 번호칸) — 5.0.105까지 거꾸로여서 칩이 길이 0으로 봤음 */
    d[3] = 0x0004;                            /* END */
    d[4] = 0; d[5] = 0; d[6] = 0;
    d[7] = 0x00010000u | s;                   /* opaque: 표준 링 + 번호 */
    slotbuf[s] = (uint16_t)buf;
    std_prod = (s + 1) & 511;
}

static int step_fail(const char *why) { net_log(why); return 0; }


static uint32_t g_hi, g_lo, g_mimode;
static int bring(void);
static void dmar_log(void);

static int probe(void)
{
    if (!nic_pci_find(0x14E4, 0x16B5, &B, &D, &F)) return 0;
    hx("NIC.bcm57785 step=found bdf", (uint32_t)(B << 8 | D << 3 | F));
    if (suspended && !halted) {                                             /* 빼기 전에 쓰던 칩을 그대로 다시 쓴다 (리셋·재설정 없음) */
        suspended = 0; ready = 1; linkup = 0; bring_busy = 0;
        net_log("NIC.bcm57785 step=resume");
        return 1;
    }
    /* 랜카드가 붙은 PCIe 브리지(루트 포트)의 버스 마스터를 켠다.
     * BIOS는 부팅에 안 쓰는 장치 쪽 브리지를 꺼 두기도 한다 → 5.0.99 로그: 첫 송신 때 칩의 메모리 읽기가 거부됨(PCI 상태 마스터 중단),
     * 상태 블록 쓰기도 안 됨. 리눅스는 장치를 켤 때 브리지도 함께 켠다. */
    for (int bb = 0; bb <= B; bb++) for (int dd = 0; dd < 32; dd++) for (int ff = 0; ff < 8; ff++) {
        uint32_t id = nic_pci_rd(bb, dd, ff, 0);
        if (id == 0xFFFFFFFFu) { if (ff == 0) break; continue; }
        if (((nic_pci_rd(bb, dd, ff, 8) >> 16) & 0xFFFF) == 0x0604) {
            uint32_t bn = nic_pci_rd(bb, dd, ff, 0x18);
            int sec = (int)((bn >> 8) & 0xFF), sub = (int)((bn >> 16) & 0xFF);
            if (sec <= B && B <= sub && sec != 0) {
                uint32_t bc = nic_pci_rd(bb, dd, ff, 4);
                hx("NIC.bcm57785 bridge_bdf", (uint32_t)(bb << 8 | dd << 3 | ff)); hx("NIC.bcm57785 bridge_cmd_before", bc & 0xFFFF);
                nic_pci_wr(bb, dd, ff, 4, (bc & 0xFFFF) | 6u);
                hx("NIC.bcm57785 bridge_cmd_after", nic_pci_rd(bb, dd, ff, 4) & 0xFFFF);
            }
        }
        if (ff == 0 && !(nic_pci_rd(bb, dd, 0, 0x0C) & 0x800000u)) break;
    }
    net_log("NIC.bcm57785 step=probe_bridge_done");
    uint32_t cmd = nic_pci_rd(B, D, F, 4);
    nic_pci_wr(B, D, F, 4, (cmd & 0xFFFF) | 6u);
    uint32_t b0 = nic_pci_rd(B, D, F, 0x10), b1 = nic_pci_rd(B, D, F, 0x14);
    uint32_t bar = b0 & ~0xFu;
    if (!bar || b1) return step_fail("NIC.bcm57785 res=fail why=bar");
    net_log("NIC.bcm57785 step=probe_uc");
    paging_set_uc(bar);
    mm = (volatile uint32_t *)(uintptr_t)bar;
    nic_pci_wr(B, D, F, 0x68, 0x9A);                                        /* 간접 접근 + PCI 상태 쓰기, 인터럽트 가림 */
    { uint32_t cp = nic_pci_rd(B, D, F, 0x34) & 0xFF; for (int g = 0; g < 32 && cp >= 0x40; g++) { uint32_t cv = nic_pci_rd(B, D, F, (int)cp); if ((cv & 0xFF) == 0x10) { pcie_cap = (int)cp; break; } cp = (cv >> 8) & 0xFF; } }
    hx("NIC.bcm57785 pcie_cap", (uint32_t)pcie_cap);
    if (pcie_cap) { hx("NIC.bcm57785 pcie_devctl_before", nic_pci_rd(B, D, F, pcie_cap + 8)); hx("NIC.bcm57785 pcie_linkctl", nic_pci_rd(B, D, F, pcie_cap + 0x10)); }
    /* 리셋 전 MAC 주소 */
    net_log("NIC.bcm57785 step=probe_mmio");
    uint32_t hi = r(0x410), lo = r(0x414);
    macaddr[0] = (uint8_t)(hi >> 8); macaddr[1] = (uint8_t)hi;
    macaddr[2] = (uint8_t)(lo >> 24); macaddr[3] = (uint8_t)(lo >> 16); macaddr[4] = (uint8_t)(lo >> 8); macaddr[5] = (uint8_t)lo;
    if ((macaddr[0] & 1) || (!macaddr[0] && !macaddr[1] && !macaddr[2] && !macaddr[3] && !macaddr[4] && !macaddr[5]))
        return step_fail("NIC.bcm57785 res=fail why=bad_mac");
    hx("NIC.bcm57785 mac_hi", hi); hx("NIC.bcm57785 mac_lo", lo);
    uint32_t mimode = r(0x454) & ~0x10u;
    hx("NIC.bcm57785 sram_sig_before", sram_rd(0x0B54));
    hx("NIC.bcm57785 sram_fwmbox_before", sram_rd(0x0B50));
    hx("NIC.bcm57785 dma_rwctrl", r(0x6C));

    g_hi = hi; g_lo = lo; g_mimode = mimode;
    if (net_skip_risky()) return step_fail("NIC.bcm57785 res=skip why=previous_run_hung (LOG\\NETGUARD.TXT 를 지우면 다시 시도)");
    /* DMA 메모리 위치를 바꿔 가며 시험 (첫 송신에서 마스터 중단이 나면 다음 위치로 다시 초기화) */
    net_guard(1);
    net_log("NIC.bcm57785 step=dmar_scan");
    dmar_log();
    net_log("NIC.bcm57785 step=dmar_done");
    net_guard(0);
    static const uint32_t bases[3] = { MEM_HDA_DMA + 0x200000u, MEM_GROW, MEM_GROW + 0x4000000u };
    int rc = 0;
    for (int k = 0; k < 1; k++) {   /* 두 번째 시도에서 멈춤 → 한 곳만 */
        dmab = bases[k];
        net_log("NIC.bcm57785 step=try_begin");
        hx("NIC.bcm57785 try.dma_base", dmab);
        paging_set_uc(dmab);
        rc = bring();
        if (rc == 1) break;
        if (rc == 0) return 0;
    }
    return rc == 1;
}

static void phy_apply(void)                                                 /* 링크가 올라왔을 때: 협상 결과로 MAC 속도·이중 맞춤 */
{
    uint32_t s1 = 0, s10 = 0, lpa = 0;
    mi_rd(0xA, &s10); mi_rd(5, &lpa); mi_rd(1, &s1);
    hx("NIC.bcm57785 phy_stat1000", s10); hx("NIC.bcm57785 phy_lpa", lpa); hx("NIC.bcm57785 phy_bmsr", s1);
    uint32_t mm_ = 0x8;
    int fd = 1;
    if (s10 & 0x0800) mm_ = 0x8;                                            /* 1000 전이중 */
    else if (s10 & 0x0400) mm_ = 0x8, fd = 0;
    else if (lpa & 0x0100) mm_ = 0x4;                                       /* 100 전이중 */
    else if (lpa & 0x0080) mm_ = 0x4, fd = 0;
    else if (lpa & 0x0040) mm_ = 0x4;                                       /* 10 전이중 */
    else mm_ = 0x4, fd = 0;
    w(0x400, MACEN | mm_ | (fd ? 0 : 2));
    hx("NIC.bcm57785 mac_mode_set", r(0x400));
}
static int bring_stage;                                         /* 초기화를 여러 번에 나눠 한다 (한 번에 0.4초 멈추던 것을 프레임마다 조금씩) */
static uint32_t bring_t0;
extern uint32_t net_now(void);
static int bring(void) { bring_stage = 0; bring_busy = 1; return 1; }      /* probe()가 부르는 곳: 시작만 한다 */
static int bring_step(void)                                                 /* 0 = 아직, 1 = 끝, -1 = 실패 (net_poll이 프레임마다 부름) */
{
    uint32_t hi = g_hi, lo = g_lo, mimode = g_mimode;
    uint32_t now = net_now();
    switch (bring_stage) {
    case 0:
        net_guard(1);                                                       /* 멈춰도 다음 부팅에서 건너뛰도록 표시 */
        net_log("NIC.bcm57785 step=reset_begin");
    sram_wr(0x0B50, 0x4B657654u);                                           /* 펌웨어 인사 표지 */
    { uint32_t g = r(0x6804) | 1u; wn(0x6804, g); }                         /* 코어 클록 리셋: 쓰기만 */
        bring_t0 = now; bring_stage = 1; return 0;
    case 1: {
        if ((int32_t)(now - bring_t0) < 120) return 0;                      /* 리셋 중 120ms: 멈추지 않고 다음 프레임에 */
        int back = nic_pci_rd(B, D, F, 0) == 0x16B514E4u;
        if (!back && (int32_t)(now - bring_t0) < 1200) return 0;
        hx("NIC.bcm57785 step=reset_pci_back", (uint32_t)back);
        if (!back) { bring_busy = 0; step_fail("NIC.bcm57785 res=fail why=no_pci_after_reset"); return -1; }
    nic_pci_wr(B, D, F, 4, 6u);
    nic_pci_wr(B, D, F, 0x68, 0x9A);
    hx("NIC.bcm57785 bar0_after", nic_pci_rd(B, D, F, 0x10));
    nic_pci_wr(B, D, F, 0x70, 0x60);                                        /* PCI 상태: ROM 켜기·재시도 (리눅스와 같게) */
    if (pcie_cap) {                                                         /* 무스눕·완화순서 끄기, 오류 상태 지우기 */
        uint32_t dc = nic_pci_rd(B, D, F, pcie_cap + 8);
        nic_pci_wr(B, D, F, pcie_cap + 8, (dc & 0xFFFFu & ~0x0810u) | 0x000F0000u);
        hx("NIC.bcm57785 pcie_devctl_after", nic_pci_rd(B, D, F, pcie_cap + 8));
    }
    w(0x4000, 2u);                                                          /* 메모리 중재 켜기 */
    w(0x6800, GRCMODE);
        bring_t0 = now; bring_stage = 2; return 0; }
    case 2: {
        int fw = sram_rd(0x0B50) == ~0x4B657654u;
        if (!fw && (int32_t)(now - bring_t0) < 3000) return 0;
        hx("NIC.bcm57785 step=reset_fw_handshake", (uint32_t)fw);
    hx("NIC.bcm57785 grc_misc_cfg_after", r(0x6804));
    hx("NIC.bcm57785 sram_fwmbox_after", sram_rd(0x0B50)); hx("NIC.bcm57785 sram_sig_after", sram_rd(0x0B54));

        bring_stage = 3; return 0; }
    case 3:
    net_log("NIC.bcm57785 step=mac_setup");
    w(0x454, mimode);                                                       /* MI 모드 (자동 폴링 끔) */
    w(0x6800, GRCMODE);                                                     /* GRC_MODE: 5.0.103 리눅스와 같게 — 설명자/자료 워드·바이트 교환 포함 */
    for (int i = 0; i < 4; i++) { w(0x410 + 8 * i, hi); w(0x414 + 8 * i, lo); }
    w(0x43C, 1522);                                                         /* 최대 받는 크기 */
    w(0x464, (2u << 12) | (6u << 8) | 32u);                                 /* TX 간격 */
    w(0x500, 0x08);                                                         /* 받기 규칙: 기본 분류 */
    /* 받기 규칙을 리눅스처럼 명시 초기화 — 펌웨어가 남긴 규칙이 프레임을 버렸을 가능성 */
    w(0x480, 0x42000000u); w(0x484, 0x7FFFFFFFu); w(0x488, 0x06000004u); w(0x48C, 0x7FFFFFFFu);
    for (int i = 4; i < 8; i++) { w(0x480 + 8 * i, 0); w(0x484 + 8 * i, 0); }
    w(0x504, 1);                                                            /* 흐름제어 때 프레임 버리지 않게 (57765 계열) */
    for (int i = 0; i < 4; i++) w(0x470 + 4 * i, 0);                        /* 멀티캐스트 해시 비움 */

    net_log("NIC.bcm57785 step=ftq_reset");
    w(0x5C00, 0xFFFFFFFFu); nic_udelay(100); w(0x5C00, 0); nic_udelay(100);

    net_log("NIC.bcm57785 step=bufmgr");
    w(0x4410, 0); w(0x4414, 0x2A); w(0x4418, 0xA0);
    w(0x4438, 5); w(0x443C, 10);
    w(0x4400, 6);
    int bm = 0;
    for (int i = 0; i < 1000; i++) { if (r(0x4400) & 2) { bm = 1; break; } nic_udelay(100); }
    hx("NIC.bcm57785 bufmgr_on", (uint32_t)bm);
    if (!bm) { bring_busy = 0; step_fail("NIC.bcm57785 res=fail why=bufmgr_not_enabled"); return -1; }

    net_log("NIC.bcm57785 step=rings");
    memset((void *)(uintptr_t)DMA, 0, 0x10000);
    std_prod = 0; ret_cons = 0; tx_prod = 0; tx_cons = 0;
    for (int i = 0; i < NRXB; i++) post_rx(i);
    /* 표준 받기 링 RCB (레지스터 쪽 0x2450) */
    w(0x2450, 0); w(0x2454, (uint32_t)(uintptr_t)STDRING);
    w(0x2458, (512u << 16) | (1536u << 2)); w(0x245C, 0x6000);
    w(0x2C18, 8);                                                           /* 호스트 보충 문턱 */
    w(0x2D00, 128);                                                        /* 보충 하한 */
    /* 받음 완료(리턴) 링 RCB: SRAM 0x200 */
    sram_wr(0x200, 0); sram_wr(0x204, (uint32_t)(uintptr_t)RETRING); sram_wr(0x208, 512u << 16); sram_wr(0x20C, 0);
    for (uint32_t a = 0x210; a < 0x200 + 16 * 16; a += 16) { sram_wr(a, 0); sram_wr(a + 4, 0); sram_wr(a + 8, 2); sram_wr(a + 12, 0); }
    /* 보내기 링 RCB: SRAM 0x100 */
    sram_wr(0x100, 0); sram_wr(0x104, (uint32_t)(uintptr_t)TXRING); sram_wr(0x108, 512u << 16); sram_wr(0x10C, 0x4000);
    for (uint32_t a = 0x110; a < 0x100 + 16 * 16; a += 16) { sram_wr(a, 0); sram_wr(a + 4, 0); sram_wr(a + 8, 2); sram_wr(a + 12, 0); }
    MB_STD(0); MB_TX(0); MB_RET(0); mbox(0x200, 0x5800, 0);                            /* 표시기 초기화 */

    net_log("NIC.bcm57785 step=hostcc");
    w(0x3C00, 0); for (int i = 0; i < 100 && (r(0x3C00) & 2); i++) nic_udelay(100);
    w(0x3C08, 5); w(0x3C0C, 5); w(0x3C10, 1); w(0x3C14, 1);
    w(0x3C18, 0); w(0x3C1C, 0); w(0x3C20, 0); w(0x3C24, 0);
    w(0x3C28, 0);
    w(0x3C38, 0); w(0x3C3C, (uint32_t)(uintptr_t)SB);                       /* 상태 블록 주소 (위 32비트 0, 아래) */
    w(0x3C00, 2 | 0x4);                                                     /* 켜기 + 지금 한 번 */

    net_log("NIC.bcm57785 step=engines");
    w(0x4900, r(0x4900) | 4u);                                              /* 읽기 DMA 예약 FIFO 넘침 보정 (57765 계열) */
    w(0x6804, (r(0x6804) & ~0xFFu) | (65u << 1));                           /* 타이머 분주 */
    w(0x2000, 2); w(0x2010, 0x0181); w(0x2018, 0xFFFFFF); w(0x2014, 1);   /* 통계 켜기 (리눅스와 같게) */
    w(0x0C0C, 0xFFFFFF); w(0x0C08, 3);                   /* 받기 목록 배치 */
    w(0x0C00, 2);                                                           /* SNDDATAI */
    w(0x1000, 2);                                                           /* SNDDATAC */
    w(0x1400, 6);                                                           /* SNDBDS */
    w(0x1800, 6);                                                           /* SNDBDI */
    w(0x1C00, 6);                                                           /* SNDBDC */
    w(0x2400, 0x12);                                                        /* RCVDBDI: 켜기 + 링크기 무효 표시 (리눅스) */
    w(0x2800, 6);                                                           /* RCVDCC */
    w(0x2C00, 6);                                                           /* RCVBDI: 켜기 + RCB 주의 */
    w(0x3000, 6);                                                           /* RCVCC */
    w(0x3400, 6);                                                           /* RCVLSC */
    w(0x4800, 0x3FE | 0x30000);                                             /* 읽기 DMA (+긴 버스트) */
    w(0x4C00, 0x3FE | 0x20000000u);                                         /* 쓰기 DMA (+상태 태그 보정) */
    nic_udelay(40);
    MB_STD(std_prod);                                                       /* 받기 버퍼 넘겨줌 */

    net_log("NIC.bcm57785 step=mac_enable");
    w(0x468, 2);                                                            /* 받기 */
    w(0x45C, 2);                                                            /* 보내기 */
    w(0x400, MACEN | 0x8 | 0x5000);                                        /* GMII 포트 + 송·수신 데이터 엔진 켬 + 통계 지움 */
    nic_udelay(100);

    net_log("NIC.bcm57785 step=phy_reset");
        mi_wr(0, 0x8000); bring_t0 = now; bring_stage = 4; return 0;
    case 4: {
        uint32_t v = 0;
        int done = !mi_rd(0, &v) && !(v & 0x8000);
        if (!done && (int32_t)(now - bring_t0) < 1000) return 0;
        hx("NIC.bcm57785 phy_bmcr_after_reset", v);
    mi_wr(4, 0x01E1); mi_wr(9, 0x0300); mi_wr(0, 0x1200);                  /* 광고: 10/100 + 1000 전이중, 자동 협상 시작 */
    net_log("NIC.bcm57785 step=phy_link");
                                                                 /* 링크는 기다리지 않는다 (MSX 화면이 멎지 않게) → link()가 올라오는 것을 보고 속도·이중을 맞춘다 */
    linkup = 0;
    hx("NIC.bcm57785 mac_mode_set", r(0x400));
    hx("NIC.bcm57785 mac_status", r(0x404));
    hx("NIC.bcm57785 rx_mode", r(0x468)); hx("NIC.bcm57785 tx_mode", r(0x45C));
    hx("NIC.bcm57785 sb_rx_prod", SB[8]); hx("NIC.bcm57785 sb_tx_cons", SB[9]);
    ready = 1; inited = 1; halted = 0;
    net_guard(0);
    /* 자체 시험: PCI 상태 지우고 → 상태 블록 갱신 요청 → 시험 프레임 1개 보내기 → 각 단계 뒤 상태 기록 */
    net_log("NIC.bcm57785 step=selftest_begin");
    nic_pci_wr(B, D, F, 4, 0xFFFF0006u);
    hx("NIC.bcm57785 st.pci_after_clear", nic_pci_rd(B, D, F, 4));
    w(0x3C00, 0x2 | 0x4);
        bring_t0 = now; bring_stage = 5; return 0; }
    case 5:
        if ((int32_t)(now - bring_t0) < 20) return 0;
    hx("NIC.bcm57785 st.sb_word0", ((uint32_t)SB[1] << 16) | SB[0]); hx("NIC.bcm57785 st.hostcc", r(0x3C00)); hx("NIC.bcm57785 st.pci_after_hostcc", nic_pci_rd(B, D, F, 4));
    { uint8_t f[60]; memset(f, 0xFF, 6); memcpy(f + 6, macaddr, 6); f[12] = 0x88; f[13] = 0xB5; memset(f + 14, 0, 46);
      tx(f, 60); }
        bring_t0 = now; bring_stage = 6; return 0;
    case 6:
        if ((int32_t)(now - bring_t0) < 20) return 0;
    hx("NIC.bcm57785 st.sb_after_tx", ((uint32_t)SB[9] << 16) | SB[8]); hx("NIC.bcm57785 st.txst", r(0x460)); hx("NIC.bcm57785 st.rdma_status", r(0x4804));
    hx("NIC.bcm57785 st.pci_after_tx", nic_pci_rd(B, D, F, 4));
    hx("NIC.bcm57785 st.sndbdi_status", r(0x1804)); hx("NIC.bcm57785 st.sndbds_status", r(0x1404));
    net_log("NIC.bcm57785 step=selftest_end");
    hx("NIC.bcm57785 res=ok link", 0u);
    if (nic_pci_rd(B, D, F, 4) & 0x20000000u) { net_log("NIC.bcm57785 try.result=master_abort"); bring_busy = 0; return -1; }
    net_log("NIC.bcm57785 try.result=no_abort");
    bring_busy = 0; return 1;
    }
    return -1;
}
static int step(void) { if (!bring_busy) return 1; return bring_step(); }
static void mac(uint8_t out[6]) { memcpy(out, macaddr, 6); }
static int link(void)
{
    static int cnt;
    if (!ready) return 0;
    static int down;
    if (++cnt >= (linkup ? 1000 : 30)) {                                    /* 올라온 뒤엔 드물게, 기다리는 중엔 자주 (약 0.5초) */
        cnt = 0; uint32_t s = 0;
        if (!mi_rd(1, &s) && !mi_rd(1, &s)) {
            int up = (s & 4) ? 1 : 0;
            if (up && !linkup) { net_log("NIC.bcm57785 step=link_up"); phy_apply(); }
            linkup = up;
        }
        if (linkup) down = 0;
        else if (++down >= 130) { down = 0; mi_wr(0, 0x1200); net_log("NIC.bcm57785 step=autoneg_restart"); }   /* 약 1분 끊겨 있으면 다시 협상 (5.0.113: 협상이 10초 넘게 걸리는 공유기가 있어 너무 자주 다시 시작하면 영영 안 올라옴) */
    }
    return linkup;
}
static int tx(const uint8_t *f, int n)
{
    if (!ready || n < 14 || n > 1514) return -1;
    uint32_t cons = SB[9] ? SB[9] : tx_soft;                                /* 상태 블록이 안 움직이면 시간으로 대신 */
    uint32_t used = (tx_prod - cons) & 511;
    if (used >= NTXB - 1) { n_txfull++; return -2; }
    int b = (int)(tx_prod % NTXB);
    memcpy(TXBUF(b), f, (size_t)n);
    int len = n < 60 ? 60 : n;
    if (len > n) memset(TXBUF(b) + n, 0, (size_t)(len - n));
    volatile uint32_t *d = TXRING + tx_prod * 4;
    d[0] = 0; d[1] = (uint32_t)(uintptr_t)TXBUF(b); d[2] = ((uint32_t)len << 16) | 0x0004; d[3] = 0;
    tx_prod = (tx_prod + 1) & 511;
    MB_TX(tx_prod); n_tx++;
    return 0;
}
static int rx(uint8_t *f, int max)
{
    if (!ready) return 0;
    { static int tick; if (++tick >= 20) { tick = 0; tx_soft = tx_prod; } }   /* 약 20ms마다 보낸 것으로 간주 (상태 블록 대용) */
    uint32_t prod = SB[8];
    if (prod == ret_cons) {
        /* 상태 블록이 안 움직여도 받기 링 칸 내용(opaque)이 채워졌으면 받은 것 */
        if (!RETRING[ret_cons * 8 + 7]) return 0;
        if (!rx_poll_mode) { rx_poll_mode = 1; net_log("NIC.bcm57785 rx_via_ring_poll=1"); }
    }
    volatile uint32_t *d = RETRING + ret_cons * 8;
    uint32_t il = d[2], err = d[5] & 0x01FF0000u & ~0x00100000u, op = d[7];
    int len = (int)(il & 0xFFFF) - 4, slot = (int)(op & 0x1FF);
    int n = 0; n_rx++;
    if (!err && len > 0 && len <= 1518 && (op & 0xFFFF0000u) == 0x00010000u) {
        n = len > max ? max : len;
        memcpy(f, RXBUF(slotbuf[slot]), (size_t)n);
    } else n_rxbad++;
    if ((op & 0xFFFF0000u) == 0x00010000u) { post_rx(slotbuf[slot]); MB_STD(std_prod); }
    d[7] = 0;
    ret_cons = (ret_cons + 1) & 511;
    MB_RET(ret_cons);
    return n;
}
static void diag(void)
{
    hx("NIC.bcm57785 d.tx_prod_cons", (tx_prod << 16) | SB[9]);
    hx("NIC.bcm57785 d.sb_rxprod_retcons", ((uint32_t)SB[8] << 16) | ret_cons);
    hx("NIC.bcm57785 d.sbstat_tag", ((uint32_t)SB[0] | ((uint32_t)SB[1] << 16)));
    hx("NIC.bcm57785 d.ntx_full", (n_tx << 16) | (n_txfull & 0xFFFF));
    hx("NIC.bcm57785 d.nrx_bad", (n_rx << 16) | (n_rxbad & 0xFFFF));
    hx("NIC.bcm57785 d.ret0_opaque_len", (RETRING[7] << 16) | (RETRING[2] & 0xFFFF)); hx("NIC.bcm57785 d.sb_w4_7", ((uint32_t)SB[5] << 16) | SB[4]);
    hx("NIC.bcm57785 d.macst", r(0x404)); hx("NIC.bcm57785 d.txst", r(0x460)); hx("NIC.bcm57785 d.rxst", r(0x46C));
    hx("NIC.bcm57785 d.macmode", r(0x400)); hx("NIC.bcm57785 d.hostcc", r(0x3C00));
    hx("NIC.bcm57785 d.rdma_mode", r(0x4800)); hx("NIC.bcm57785 d.wdma_mode", r(0x4C00));
    hx("NIC.bcm57785 d.rdma_st_wdma_st", (r(0x4804) << 16) | (r(0x4C04) & 0xFFFF));
    hx("NIC.bcm57785 d.sndbdi_sndbds_sndbdc", ((r(0x1800) & 0xFF) << 16) | ((r(0x1400) & 0xFF) << 8) | (r(0x1C00) & 0xFF));
    hx("NIC.bcm57785 d.rcvlpc_rcvdbdi_rcvcc", ((r(0x2000) & 0xFF) << 16) | ((r(0x2400) & 0xFF) << 8) | (r(0x3000) & 0xFF));
    hx("NIC.bcm57785 d.bufmgr_memarb_grcmode", ((r(0x4400) & 0xFF) << 24) | ((r(0x4000) & 0xFF) << 16) | (r(0x6800) >> 16));
    hx("NIC.bcm57785 d.pcicmd_status", nic_pci_rd(B, D, F, 4));
    hx("NIC.bcm57785 d.rcb_tx_hi", sram_rd(0x100)); hx("NIC.bcm57785 d.rcb_tx_lo", sram_rd(0x104)); hx("NIC.bcm57785 d.rcb_tx_flags", sram_rd(0x108)); hx("NIC.bcm57785 d.rcb_tx_nic", sram_rd(0x10C));
    hx("NIC.bcm57785 d.rcb_ret_lo_flags", sram_rd(0x204)); hx("NIC.bcm57785 d.rcb_ret_flags", sram_rd(0x208));
    hx("NIC.bcm57785 d.rcb_std_hi", r(0x2450)); hx("NIC.bcm57785 d.rcb_std_lo", r(0x2454)); hx("NIC.bcm57785 d.rcb_std_flags", r(0x2458));
    hx("NIC.bcm57785 d.sb_addr_hi", r(0x3C38)); hx("NIC.bcm57785 d.sb_addr_lo", r(0x3C3C));
    hx("NIC.bcm57785 d.tx_bd0_lo_len", (TXRING[1] ) ); hx("NIC.bcm57785 d.tx_bd0_lenflags", TXRING[2]);
    hx("NIC.bcm57785 d.pcie_devctl", nic_pci_rd(B, D, F, pcie_cap ? pcie_cap + 8 : 0));
    hx("NIC.bcm57785 d.dma_rwctrl", r(0x6C));
    { uint32_t b = 0, an = 0, c9 = 0, au = 0; mi_rd(0, &b); mi_rd(4, &an); mi_rd(9, &c9); mi_rd(0x19, &au);
      hx("NIC.bcm57785 d.phy_bmcr_anar", (b << 16) | an); hx("NIC.bcm57785 d.phy_c1000_aux19", (c9 << 16) | au); }
    hx("NIC.bcm57785 d.mtx_octets_ucast", r(0x800)); hx("NIC.bcm57785 d.mtx_ucast_bcast", (r(0x86C) << 16) | (r(0x874) & 0xFFFF));
    hx("NIC.bcm57785 d.mrx_octets", r(0x880)); hx("NIC.bcm57785 d.mrx_ucast_mcast_bcast", ((r(0x88C) & 0xFF) << 16) | ((r(0x890) & 0xFF) << 8) | (r(0x894) & 0xFF));
    hx("NIC.bcm57785 d.mrx_fcs_align_long_short", ((r(0x898) & 0xFF) << 24) | ((r(0x89C) & 0xFF) << 16) | ((r(0x8B0) & 0xFF) << 8) | (r(0x8B8) & 0xFF));
    hx("NIC.bcm57785 d.lpc_drop_nobd_disc_err", ((r(0x2240) & 0xFF) << 24) | ((r(0x224C) & 0xFF) << 16) | ((r(0x2250) & 0xFF) << 8) | (r(0x2254) & 0xFF));
    hx("NIC.bcm57785 d.std_prod_mbox", r(0x270)); hx("NIC.bcm57785 d.rcvbdi_status", r(0x2C04)); hx("NIC.bcm57785 d.rxlpc_status", r(0x2004));
    hx("NIC.bcm57785 d.bmsr", ({ uint32_t s = 0; mi_rd(1, &s); mi_rd(1, &s); s; }));
}
static void stop(void)                                                      /* 카드를 뺄 때(일시 정지): 칩은 그대로 두고 쓰는 것만 멈춘다. 한 번 쓴 칩을 다시 리셋하면 멈추고(5.0.120·121), 엔진만 껐다 켜면 보내기가 안 나갔다(5.0.125 로그) → 다시 꽂을 때는 그대로 이어서 쓴다 */
{
    ready = 0; linkup = 0; suspended = inited;
    net_log("NIC.bcm57785 step=suspend");
}
void nic_bcm57785_halt(void)                                                /* 재부팅·전원 끄기 직전: 칩이 메모리에 쓰지 않도록 엔진을 끄고 버스 마스터를 끈다 */
{
    if (!inited || halted) return;
    halted = 1; ready = 0; linkup = 0; suspended = 0;
    net_log("NIC.bcm57785 step=halt_begin");
    w(0x468, 0); w(0x45C, 0); nic_udelay(10000);
    w(0x3000, 0); w(0x3400, 0); w(0x2C00, 0); w(0x2800, 0); w(0x2400, 0);
    w(0x1C00, 0); w(0x1800, 0); w(0x1400, 0); w(0x1000, 0); w(0x0C00, 0);
    w(0x4800, 0); w(0x4C00, 0); w(0x3C00, 0);
    nic_udelay(20000);
    nic_pci_wr(B, D, F, 4, 0u);
    net_log("NIC.bcm57785 step=halt_done");
}
const Nic nic_bcm57785 = { "bcm57785", probe, mac, link, tx, rx, diag, stop, step };

/* 메모리 보호(VT-d/IOMMU) 표가 있는지 기록 — 랜카드 DMA 거부 원인 후보 */
static uint32_t rd32m(uint32_t a) { return *(volatile uint32_t *)(uintptr_t)a; }
static void dmar_log(void)
{
    uint32_t rs = 0;
    uint32_t ebda = (uint32_t)(*(volatile uint16_t *)0x40E) << 4;
    for (uint32_t a = ebda; ebda && a < ebda + 1024; a += 16) if (rd32m(a) == 0x20445352u && rd32m(a + 4) == 0x20525450u) { rs = a; break; }
    for (uint32_t a = 0xE0000; !rs && a < 0x100000; a += 16) if (rd32m(a) == 0x20445352u && rd32m(a + 4) == 0x20525450u) { rs = a; break; }
    hx("NIC.bcm57785 acpi.rsdp", rs);
    if (!rs) return;
    uint32_t rsdt = rd32m(rs + 16);
    if (!rsdt || rsdt > 0xF0000000u) return;
    uint32_t len = rd32m(rsdt + 4), n = (len > 36) ? (len - 36) / 4 : 0;
    if (n > 64) n = 64;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t t = rd32m(rsdt + 36 + 4 * i);
        if (!t || t > 0xF0000000u) continue;
        if (rd32m(t) == 0x52414D44u) {                                   /* "DMAR" */
            uint32_t tl = rd32m(t + 4);
            hx("NIC.bcm57785 dmar.table", t); hx("NIC.bcm57785 dmar.len", tl);
            uint32_t o = 48;
            while (o + 8 <= tl && o < 4096) {
                uint32_t h = rd32m(t + o); uint32_t ty = h & 0xFFFF, sl = h >> 16;
                if (sl < 8) break;
                if (ty == 0) {
                    uint32_t bs = rd32m(t + o + 8);
                    hx("NIC.bcm57785 dmar.drhd_base", bs);
                } else if (ty == 1) {
                    hx("NIC.bcm57785 dmar.rmrr_base", rd32m(t + o + 8)); hx("NIC.bcm57785 dmar.rmrr_limit", rd32m(t + o + 16));
                }
                o += sl;
            }
        }
    }
}
