/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* usbnic_r8152.c - 5.2.110 USB 유선 랜 드라이버: Realtek RTL8152/8153/8153B (제조사 전용 인터페이스, OCP 레지스터 방식)
 * 레지스터 순서·값은 U-Boot drivers/usb/eth/r8152.c (GPL-2.0, Realtek) 를 참고해 UEFI USB_IO 로 다시 씀 (THIRD-PARTY-NOTICES.txt).
 * 펌웨어(UEFI) USB 스택은 장치의 첫 구성(제조사 전용)만 열어 주므로 CDC-ECM 모드로는 못 쓰고 이 방식이 필요함.
 * 처음엔 PHY 보정 펌웨어 패치(r8153_firmware)는 싣지 않는다. 모든 단계는 net_log 로 USB 에 남긴다 (NET.usb ...). 5.2.110: 미시험 → 1번 노트북 RTL8153 로 확인 */
#include "efi.h"
#include "../kernel/net/nic.h"
#include "../kernel/net/nicdb.h"
#include <string.h>

#define EFI_TO (0x8000000000000000ull | 18)
static EFI_BOOT_SERVICES *UBS;
extern void usbnic_asix_init(EFI_BOOT_SERVICES *bs);
void usbnic_init(EFI_BOOT_SERVICES *bs) { UBS = bs; usbnic_asix_init(bs); }

#define MCU_PLA 0x0100
#define MCU_USB 0x0000
#define PLA_IDR 0xc000
#define PLA_RCR 0xc010
#define PLA_RMS 0xc016
#define PLA_RXFIFO_CTRL0 0xc0a0
#define PLA_RXFIFO_CTRL1 0xc0a4
#define PLA_RXFIFO_CTRL2 0xc0a8
#define PLA_DMY_REG0 0xc0b0
#define PLA_FMC 0xc0b4
#define PLA_TEREDO_CFG 0xc0bc
#define PLA_MAR 0xcd00
#define PLA_BDC_CR 0xd1a0
#define PLA_EFUSE_DATA 0xdd00
#define PLA_EFUSE_CMD 0xdd02
#define PLA_LED_FEATURE 0xdd92
#define PLA_BOOT_CTRL 0xe004
#define PLA_GPHY_INTR_IMR 0xe022
#define PLA_EEEP_CR 0xe080
#define PLA_MAC_PWR_CTRL 0xe0c0
#define PLA_MAC_PWR_CTRL2 0xe0ca
#define PLA_MAC_PWR_CTRL3 0xe0cc
#define PLA_TCR0 0xe610
#define PLA_TCR1 0xe612
#define PLA_MTPS 0xe615
#define PLA_TXFIFO_CTRL 0xe618
#define PLA_RSTTALLY 0xe800
#define BIST_CTRL 0xe810
#define PLA_CR 0xe813
#define PLA_CRWECR 0xe81c
#define PLA_PHY_PWR 0xe84c
#define PLA_OOB_CTRL 0xe84f
#define PLA_CPCR 0xe854
#define PLA_MISC_1 0xe85a
#define PLA_OCP_GPHY_BASE 0xe86c
#define PLA_SFF_STS_7 0xe8de
#define PLA_PHYSTATUS 0xe908
#define USB_USB2PHY 0xb41e
#define USB_SSPHYLINK2 0xb428
#define USB_U2P3_CTRL 0xb460
#define USB_CSR_DUMMY1 0xb464
#define USB_CSR_DUMMY2 0xb466
#define USB_CONNECT_TIMER 0xcbf8
#define USB_MSC_TIMER 0xcbfc
#define USB_BURST_SIZE 0xcfc0
#define USB_LPM_CONFIG 0xcfd8
#define USB_USB_CTRL 0xd406
#define USB_TX_AGG 0xd40a
#define USB_RX_BUF_TH 0xd40c
#define USB_USB_TIMER 0xd428
#define USB_RX_EARLY_TIMEOUT 0xd42c
#define USB_RX_EARLY_SIZE 0xd42e
#define USB_PM_CTRL_STATUS 0xd432
#define USB_RX_EXTRA_AGGR_TMR 0xd432
#define USB_TX_DMA 0xd434
#define USB_UPT_RXDMA_OWN 0xd437
#define USB_TOLERANCE 0xd490
#define USB_LPM_CTRL 0xd41a
#define USB_BMU_RESET 0xd4b0
#define USB_U1U2_TIMER 0xd4da
#define USB_UPS_CTRL 0xd800
#define USB_POWER_CUT 0xd80a
#define USB_MISC_0 0xd81a
#define USB_AFE_CTRL2 0xd824
#define USB_UPS_CFG 0xd842
#define USB_WDT11_CTRL 0xe43c
#define OCP_ALDPS_CONFIG 0x2010
#define OCP_BASE_MII 0xa400
#define OCP_PHY_STATUS 0xa420
#define OCP_NCTL_CFG 0xa42c
#define OCP_POWER_CFG 0xa430
#define OCP_SRAM_ADDR 0xa436
#define OCP_SRAM_DATA 0xa438
#define OCP_ADC_IOFFSET 0xbcfc
#define SRAM_GREEN_CFG 0x8011
#define RCR_APM 0x2
#define RCR_AM 0x4
#define RCR_AB 0x8
#define RCR_ALL 0xf
#define TCR0_TX_EMPTY 0x0800
#define TCR0_AUTO_FIFO 0x0080
#define VERSION_MASK 0x7cf0
#define MTPS_JUMBO (12 * 1024 / 64)
#define PLA_CR_RE 0x08
#define PLA_CR_TE 0x04
#define BIST_SW_RESET (0x10u << 24)
#define CRWECR_NORMAL 0x00
#define NOW_IS_OOB 0x80
#define DIS_MCU_CLROOB 0x01
#define PHY_PWR_LLR (0x02u << 24)
#define PHY_PWR_TXEMP (0x20u << 24)
#define TEST_IO_OFF 0x10
#define RXDY_GATED_EN 0x0008
#define RE_INIT_LL 0x8000
#define MCU_BORW_EN 0x4000
#define CPCR_RX_VLAN 0x0040
#define TEREDO_WAKE_MASK 0x7f00
#define ALDPS_PROXY_MODE 0x0001
#define EFUSE_READ_CMD 0x8000
#define EFUSE_DATA_BIT16 0x80
#define LED_MODE_MASK 0x0700
#define TX_10M_IDLE_EN 0x0080
#define PFM_PWM_SWITCH 0x0040
#define D3_CLK_GATED_EN 0x00004000
#define MCU_CLK_RATIO 0x07010f07
#define MCU_CLK_RATIO_MASK 0x0f0f0f0f
#define MAC_CLK_SPDWN_EN 0x8000
#define PLA_MCU_SPDWN_EN 0x4000
#define GPHY_INTR 0x000f
#define AUTOLOAD_DONE 0x0002
#define USB2PHY_SUSPEND 1
#define USB2PHY_L1 2
#define DYNAMIC_BURST 1
#define EP4_FULL_FC 1
#define LPM_U1U2_EN 1
#define RXFIFO_THR1_NORMAL 0x00080002
#define RXFIFO_THR1_OOB 0x01800003
#define RXFIFO_THR2_FULL 0x60
#define RXFIFO_THR2_HIGH 0x38
#define RXFIFO_THR2_OOB 0x4a
#define RXFIFO_THR2_NORMAL 0x00a0
#define RXFIFO_THR3_FULL 0x78
#define RXFIFO_THR3_HIGH 0x48
#define RXFIFO_THR3_OOB 0x5a
#define RXFIFO_THR3_NORMAL 0x0110
#define TXFIFO_THR_NORMAL 0x00400008
#define TXFIFO_THR_NORMAL2 0x01000008
#define TX_AGG_MAX_THRESHOLD 3
#define RX_THR_HIGH 0x7a120180
#define TEST_MODE_DISABLE 1
#define TX_SIZE_ADJUST1 0x100
#define BMU_RESET_EP_IN 1
#define BMU_RESET_EP_OUT 2
#define OWN_UPDATE 1
#define OWN_CLEAR 2
#define POWER_CUT 0x0100
#define RESUME_INDICATE 1
#define RX_AGG_DISABLE 0x10
#define RX_ZERO_EN 0x80
#define U2P3_ENABLE 1
#define PWR_EN 1
#define PHASE2_EN 8
#define PCUT_STATUS 1
#define TIMER11_EN 1
#define FIFO_EMPTY_1FB 0x30
#define LPM_TIMER_500MS 0x04
#define LPM_TIMER_500US 0x0c
#define ROK_EXIT_LPM 0x02
#define SEN_VAL_MASK 0xf800
#define SEN_VAL_NORMAL 0xa000
#define SEL_RXIDLE 0x0100
#define SAW_CNT_1MS_MASK 0x0fff
#define ENPWRSAVE 0x8000
#define ENPDNPS 0x0200
#define LINKENA 0x0100
#define DIS_SDSAVE 0x0010
#define PHY_STAT_MASK 7
#define PHY_STAT_LAN_ON 3
#define PHY_STAT_PWRDN 5
#define PGA_RETURN_EN 2
#define EN_ALDPS 0x0004
#define R_TUNE_EN 0x0800
#define TALLY_RESET 1
#define LINK_STATUS 0x02
#define RMS 1522          /* 1518 + 4 (U-Boot: RTL8152_ETH_FRAME_LEN 1514 + CRC) */
#define RX_BUF 2048
#define TX_DESC 8
#define RX_DESC 24
#define MII_BMCR 0
#define MII_ADVERTISE 4
#define MII_CTRL1000 9
#define BMCR_PDOWN 0x0800
#define BMCR_ANENABLE 0x1000
#define BMCR_ANRESTART 0x0200
#define BMCR_RESET 0x8000
#define ADV_ALL10_100 0x01e0
#define ADV_PAUSE 0x0c00
#define ADV_1000 0x0300

enum { V1 = 1, V2, V3, V4, V5, V6, V7, V8, V9 };
static const struct { uint16_t tcr; uint8_t ver, gmii; } vt[] = {
    { 0x4c00, V1, 0 }, { 0x4c10, V2, 0 }, { 0x5c00, V3, 1 }, { 0x5c10, V4, 1 }, { 0x5c20, V5, 1 },
    { 0x5c30, V6, 1 }, { 0x4800, V7, 0 }, { 0x6000, V8, 1 }, { 0x6010, V9, 1 },
};

static EFI_USB_IO_PROTOCOL *io;
static uint8_t ep_in, ep_out;
static uint16_t mps_out = 512, bcd_usb;
static int ver, gmii, super_speed, phase, nerr, link_up, tx_n, tx_bad, rx_n, rx_bad;
static uint16_t ocp_base = 0xFFFF;
static uint8_t macaddr[6];
static uint32_t link_next;
static uint8_t cbuf[512] __attribute__((aligned(64)));
static uint8_t txb[2048] __attribute__((aligned(64)));
static uint8_t rxb[RX_BUF] __attribute__((aligned(64)));
static int rx_len, rx_pos;

static void ulog(const char *k, uint32_t v) { net_log_hex(k, v); }
static void mdelay(int ms) { if (UBS) UBS->Stall((UINTN)ms * 1000); }

static int creg(int wr, uint16_t value, uint16_t index, uint16_t n)
{
    EFI_USB_DEVICE_REQUEST rq; uint32_t st = 0;
    rq.RequestType = wr ? 0x40 : 0xC0; rq.Request = 0x05; rq.Value = value; rq.Index = index; rq.Length = n;
    EFI_STATUS s = io->UsbControlTransfer(io, &rq, wr ? EfiUsbDataOut : EfiUsbDataIn, 500, cbuf, n, &st);
    if (s != EFI_SUCCESS) { if (nerr < 6) { net_log_hex("NET.usb ctl_err idx", index); net_log_hex("NET.usb ctl_err val", value); net_log_hex("NET.usb ctl_err st", (uint32_t)(s & 0xFFFF)); } nerr++; return -1; }
    return 0;
}
static int ocp_read(uint16_t index, uint16_t size, void *data, uint16_t type)
{
    uint8_t *d = data;
    while (size) {
        uint16_t t = size > 64 ? 64 : size;
        if (creg(0, index, type, t)) return -1;
        memcpy(d, cbuf, t); index += t; d += t; size -= t;
    }
    return 0;
}
static int ocp_write(uint16_t index, uint16_t byteen, uint16_t size, const void *data, uint16_t type)
{
    const uint8_t *d = data;
    uint16_t bs = byteen & 0x0f, be = byteen & 0xf0;
    memcpy(cbuf, d, 4);
    if (creg(1, index, type | bs | (bs << 4), 4)) return -1;
    index += 4; d += 4; size -= 4;
    if (size) {
        size -= 4;
        while (size) {
            uint16_t t = size > 256 ? 256 : size;
            memcpy(cbuf, d, t);
            if (creg(1, index, type | 0xff, t)) return -1;
            index += t; d += t; size -= t;
        }
        memcpy(cbuf, d, 4);
        if (creg(1, index, type | be | (be >> 4), 4)) return -1;
    }
    return 0;
}
static uint32_t rd32(uint16_t t, uint16_t i) { uint8_t b[4] = { 0 }; ocp_read(i, 4, b, t); return b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24; }
static void wr32(uint16_t t, uint16_t i, uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; ocp_write(i, 0xff, 4, b, t); }
static uint16_t rd16(uint16_t t, uint16_t i) { uint8_t sh = i & 2; return (uint16_t)(rd32(t, i & ~3) >> (sh * 8)); }
static void wr16(uint16_t t, uint16_t i, uint32_t v)
{
    uint8_t sh = i & 2; v &= 0xffff;
    uint16_t be = 0x33;
    if (sh) { be <<= 2; v <<= 16; i &= ~3; }
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    ocp_write(i, be, 4, b, t);
}
static uint8_t rd8(uint16_t t, uint16_t i) { uint8_t sh = i & 3; return (uint8_t)(rd32(t, i & ~3) >> (sh * 8)); }
static void wr8(uint16_t t, uint16_t i, uint32_t v)
{
    uint8_t sh = i & 3; v &= 0xff;
    uint16_t be = 0x11;
    if (sh) { be <<= sh; v <<= (sh * 8); i &= ~3; }
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    ocp_write(i, be, 4, b, t);
}
static uint16_t reg_r(uint16_t a)
{
    uint16_t base = a & 0xf000;
    if (base != ocp_base) { wr16(MCU_PLA, PLA_OCP_GPHY_BASE, base); ocp_base = base; }
    return rd16(MCU_PLA, (a & 0x0fff) | 0xb000);
}
static void reg_w(uint16_t a, uint16_t d)
{
    uint16_t base = a & 0xf000;
    if (base != ocp_base) { wr16(MCU_PLA, PLA_OCP_GPHY_BASE, base); ocp_base = base; }
    wr16(MCU_PLA, (a & 0x0fff) | 0xb000, d);
}
static uint16_t mdio_r(uint16_t r) { return reg_r(OCP_BASE_MII + r * 2); }
static void mdio_w(uint16_t r, uint16_t v) { reg_w(OCP_BASE_MII + r * 2, v); }
static void sram_w(uint16_t a, uint16_t d) { reg_w(OCP_SRAM_ADDR, a); reg_w(OCP_SRAM_DATA, d); }
static uint16_t sram_r(uint16_t a) { reg_w(OCP_SRAM_ADDR, a); return reg_r(OCP_SRAM_DATA); }
static int wait_bit(uint16_t type, uint16_t idx, uint32_t mask, int set)
{
    for (int i = 0; i < 2000; i++) {
        uint32_t v = rd32(type, idx);
        if (!set) v = ~v;
        if ((v & mask) == mask) return 0;
        mdelay(1);
    }
    return -1;
}

static void pkt_filter_reset(void) { uint32_t d = rd16(MCU_PLA, PLA_FMC); d &= ~1u; wr16(MCU_PLA, PLA_FMC, d); d |= 1; wr16(MCU_PLA, PLA_FMC, d); }
static void fifo_empty(void) { wait_bit(MCU_PLA, PLA_PHY_PWR, PHY_PWR_TXEMP, 1); wait_bit(MCU_PLA, PLA_TCR0, TCR0_TX_EMPTY, 1); }
static void nic_reset(void) { wr32(MCU_PLA, BIST_CTRL, rd32(MCU_PLA, BIST_CTRL) | BIST_SW_RESET); wait_bit(MCU_PLA, BIST_CTRL, BIST_SW_RESET, 0); }
static void rxdy(int en) { uint32_t d = rd16(MCU_PLA, PLA_MISC_1); if (en) d |= RXDY_GATED_EN; else d &= ~RXDY_GATED_EN; wr16(MCU_PLA, PLA_MISC_1, d); }
static void rx_mode(void)
{
    uint8_t m[8]; memset(m, 0xff, 8);
    ocp_write(PLA_MAR, 0xff, 8, m, MCU_PLA);
    wr32(MCU_PLA, PLA_RCR, rd32(MCU_PLA, PLA_RCR) | RCR_APM | RCR_AM | RCR_AB);
}
static void rx_vlan(int en) { uint32_t d = rd16(MCU_PLA, PLA_CPCR); if (en) d |= CPCR_RX_VLAN; else d &= ~CPCR_RX_VLAN; wr16(MCU_PLA, PLA_CPCR, d); }
static void reinit_ll(void)
{
    wait_bit(MCU_PLA, PLA_PHY_PWR, PHY_PWR_LLR, 1);
    wr16(MCU_PLA, PLA_SFF_STS_7, rd16(MCU_PLA, PLA_SFF_STS_7) | RE_INIT_LL);
    wait_bit(MCU_PLA, PLA_PHY_PWR, PHY_PWR_LLR, 1);
}
static void bmu_reset(void)
{
    uint8_t d = rd8(MCU_USB, USB_BMU_RESET);
    d &= ~(BMU_RESET_EP_IN | BMU_RESET_EP_OUT); wr8(MCU_USB, USB_BMU_RESET, d);
    d |= BMU_RESET_EP_IN | BMU_RESET_EP_OUT; wr8(MCU_USB, USB_BMU_RESET, d);
}
static void rtl_disable(void)
{
    wr32(MCU_PLA, PLA_RCR, rd32(MCU_PLA, PLA_RCR) & ~(uint32_t)RCR_ALL);
    rxdy(1); fifo_empty(); nic_reset();
}
static void is8153b(int *r) { *r = ver == V8 || ver == V9; }
static void rtl_enable(void)
{
    int b; is8153b(&b);
    pkt_filter_reset();
    wr8(MCU_PLA, PLA_CR, rd8(MCU_PLA, PLA_CR) | PLA_CR_RE | PLA_CR_TE);
    if (b) wr8(MCU_USB, USB_UPT_RXDMA_OWN, OWN_UPDATE | OWN_CLEAR);
    rxdy(0); rx_mode();
}
static void eee_plus(void) { wr16(MCU_PLA, PLA_EEEP_CR, rd16(MCU_PLA, PLA_EEEP_CR) & ~2u); }
static void rx_early(void)
{
    uint32_t coal = (super_speed ? 85000U : 250000U) / 8;
    uint32_t sz = RX_BUF - RMS - RX_DESC;
    if (ver >= V3 && ver <= V6) { wr16(MCU_USB, USB_RX_EARLY_TIMEOUT, coal); wr16(MCU_USB, USB_RX_EARLY_SIZE, sz / 4); }
    else if (ver == V8 || ver == V9) { wr16(MCU_USB, USB_RX_EARLY_TIMEOUT, 1264 / 8); wr16(MCU_USB, USB_RX_EXTRA_AGGR_TMR, coal); wr16(MCU_USB, USB_RX_EARLY_SIZE, sz / 8); }
}
static void enable(void)
{
    eee_plus();
    if (ver >= V3) rx_early();
    rtl_enable();
}
static void power_cut8152(int en)
{
    uint32_t d = rd16(MCU_USB, USB_UPS_CTRL); if (en) d |= POWER_CUT; else d &= ~POWER_CUT; wr16(MCU_USB, USB_UPS_CTRL, d);
    d = rd16(MCU_USB, USB_PM_CTRL_STATUS); d &= ~RESUME_INDICATE; wr16(MCU_USB, USB_PM_CTRL_STATUS, d);
}
static void power_cut8153(int en)
{
    uint32_t d = rd16(MCU_USB, USB_POWER_CUT); if (en) d |= PWR_EN | PHASE2_EN; else d &= ~(uint32_t)(PWR_EN | PHASE2_EN); wr16(MCU_USB, USB_POWER_CUT, d);
    d = rd16(MCU_USB, USB_MISC_0); d &= ~(uint32_t)PCUT_STATUS; wr16(MCU_USB, USB_MISC_0, d);
}
static void u1u2en(int en)
{
    uint8_t u[8]; memset(u, en ? 0xff : 0, 8);
    ocp_write(USB_TOLERANCE, 0x3f, 8, u, MCU_USB);
}
static void u1u2en_b(int en) { uint32_t d = rd16(MCU_USB, USB_LPM_CONFIG); if (en) d |= LPM_U1U2_EN; else d &= ~(uint32_t)LPM_U1U2_EN; wr16(MCU_USB, USB_LPM_CONFIG, d); }
static void u2p3en(int en)
{
    uint32_t d = rd16(MCU_USB, USB_U2P3_CTRL);
    if (en && ver != V3 && ver != V4) d |= U2P3_ENABLE; else d &= ~(uint32_t)U2P3_ENABLE;
    wr16(MCU_USB, USB_U2P3_CTRL, d);
}
static void aldps8152(int en) { reg_w(OCP_ALDPS_CONFIG, (en ? ENPWRSAVE : 0) | ENPDNPS | LINKENA | DIS_SDSAVE); if (!en) mdelay(20); }
static void aldps8153_off(void) { uint16_t d = reg_r(OCP_POWER_CFG); d &= ~EN_ALDPS; reg_w(OCP_POWER_CFG, d); mdelay(20); }
static void enable_fc(void) { mdio_w(MII_ADVERTISE, mdio_r(MII_ADVERTISE) | ADV_PAUSE); }
static void tally_reset(void) { wr16(MCU_PLA, PLA_RSTTALLY, rd16(MCU_PLA, PLA_RSTTALLY) | TALLY_RESET); }
static uint32_t efuse_read(uint8_t a)
{
    wr16(MCU_PLA, PLA_EFUSE_CMD, EFUSE_READ_CMD | a);
    uint32_t d = rd16(MCU_PLA, PLA_EFUSE_CMD);
    d = (d & EFUSE_DATA_BIT16) << 9;
    return d | rd16(MCU_PLA, PLA_EFUSE_DATA);
}

/* ---- 8152 ---- */
static void exit_oob_8152(void)
{
    wr32(MCU_PLA, PLA_RCR, rd32(MCU_PLA, PLA_RCR) & ~(uint32_t)RCR_ALL);
    rxdy(1);
    { uint16_t d = mdio_r(MII_BMCR); if (d & BMCR_PDOWN) mdio_w(MII_BMCR, d & ~BMCR_PDOWN); }
    wr8(MCU_PLA, PLA_CRWECR, CRWECR_NORMAL);
    wr8(MCU_PLA, PLA_CR, 0);
    wr8(MCU_PLA, PLA_OOB_CTRL, rd8(MCU_PLA, PLA_OOB_CTRL) & ~NOW_IS_OOB);
    wr16(MCU_PLA, PLA_SFF_STS_7, rd16(MCU_PLA, PLA_SFF_STS_7) & ~(uint32_t)MCU_BORW_EN);
    reinit_ll(); nic_reset();
    wr32(MCU_PLA, PLA_RXFIFO_CTRL0, RXFIFO_THR1_NORMAL);
    wr32(MCU_PLA, PLA_RXFIFO_CTRL1, RXFIFO_THR2_HIGH);
    wr32(MCU_PLA, PLA_RXFIFO_CTRL2, RXFIFO_THR3_HIGH);
    wr32(MCU_PLA, PLA_TXFIFO_CTRL, TXFIFO_THR_NORMAL);
    wr8(MCU_USB, USB_TX_AGG, TX_AGG_MAX_THRESHOLD);
    wr32(MCU_USB, USB_RX_BUF_TH, RX_THR_HIGH);
    wr32(MCU_USB, USB_TX_DMA, TEST_MODE_DISABLE | TX_SIZE_ADJUST1);
    wr16(MCU_PLA, PLA_RMS, RMS);
    wr16(MCU_PLA, PLA_TCR0, rd16(MCU_PLA, PLA_TCR0) | TCR0_AUTO_FIFO);
}
static void init_8152(void)
{
    aldps8152(0);
    if (ver == V1) wr16(MCU_PLA, PLA_LED_FEATURE, rd16(MCU_PLA, PLA_LED_FEATURE) & ~(uint32_t)LED_MODE_MASK);
    power_cut8152(0);
    wr16(MCU_PLA, PLA_PHY_PWR, rd16(MCU_PLA, PLA_PHY_PWR) | TX_10M_IDLE_EN | PFM_PWM_SWITCH);
    { uint32_t d = rd32(MCU_PLA, PLA_MAC_PWR_CTRL); d &= ~(uint32_t)MCU_CLK_RATIO_MASK; d |= MCU_CLK_RATIO | D3_CLK_GATED_EN; wr32(MCU_PLA, PLA_MAC_PWR_CTRL, d); }
    wr16(MCU_PLA, PLA_GPHY_INTR_IMR, GPHY_INTR);
    { uint32_t d = rd16(MCU_USB, USB_USB_TIMER); d |= 0x8000; wr16(MCU_USB, USB_USB_TIMER, d); wr16(MCU_USB, 0xcbfc, 0x03e8); d &= ~0x8000u; wr16(MCU_USB, USB_USB_TIMER, d); }
    enable_fc(); tally_reset();
    wr16(MCU_USB, USB_USB_CTRL, rd16(MCU_USB, USB_USB_CTRL) & ~(uint32_t)(RX_AGG_DISABLE | RX_ZERO_EN));
}

/* ---- 8153 / 8153B ---- */
static void first_init_8153(void)
{
    rxdy(1);
    wr32(MCU_PLA, PLA_RCR, rd32(MCU_PLA, PLA_RCR) & ~(uint32_t)RCR_ALL);
    if (ver != V8 && ver != V9) {          /* r8153_hw_phy_cfg: 펌웨어 패치 제외 */
        { uint16_t d = mdio_r(MII_BMCR); if (d & BMCR_PDOWN) mdio_w(MII_BMCR, d & ~BMCR_PDOWN); }
    }
    nic_reset(); bmu_reset();
    wr8(MCU_PLA, PLA_OOB_CTRL, rd8(MCU_PLA, PLA_OOB_CTRL) & ~NOW_IS_OOB);
    wr16(MCU_PLA, PLA_SFF_STS_7, rd16(MCU_PLA, PLA_SFF_STS_7) & ~(uint32_t)MCU_BORW_EN);
    reinit_ll(); rx_vlan(0);
    wr16(MCU_PLA, PLA_RMS, RMS);
    wr8(MCU_PLA, PLA_MTPS, MTPS_JUMBO);
    wr16(MCU_PLA, PLA_TCR0, rd16(MCU_PLA, PLA_TCR0) | TCR0_AUTO_FIFO);
    nic_reset();
    wr32(MCU_PLA, PLA_RXFIFO_CTRL0, RXFIFO_THR1_NORMAL);
    wr16(MCU_PLA, PLA_RXFIFO_CTRL1, RXFIFO_THR2_NORMAL);
    wr16(MCU_PLA, PLA_RXFIFO_CTRL2, RXFIFO_THR3_NORMAL);
    wr32(MCU_PLA, PLA_TXFIFO_CTRL, TXFIFO_THR_NORMAL2);
    wr16(MCU_USB, USB_USB_CTRL, rd16(MCU_USB, USB_USB_CTRL) & ~(uint32_t)(RX_AGG_DISABLE | RX_ZERO_EN));
}
static void init_8153(void)
{
    aldps8153_off(); u1u2en(0);
    wait_bit(MCU_PLA, PLA_BOOT_CTRL, AUTOLOAD_DONE, 1);
    for (int i = 0; i < 2000; i++) { uint16_t s = reg_r(OCP_PHY_STATUS) & PHY_STAT_MASK; if (s == PHY_STAT_LAN_ON || s == PHY_STAT_PWRDN) break; mdelay(1); }
    u2p3en(0);
    if (ver == V4) {
        uint32_t d = rd16(MCU_USB, USB_SSPHYLINK2); d &= ~0x3ffeu; d |= (96u << 1); wr16(MCU_USB, USB_SSPHYLINK2, d);
        wr8(MCU_USB, USB_USB2PHY, rd8(MCU_USB, USB_USB2PHY) | USB2PHY_L1 | USB2PHY_SUSPEND);
    } else if (ver == V5) {
        wr8(MCU_PLA, PLA_DMY_REG0, rd8(MCU_PLA, PLA_DMY_REG0) & ~2);
        uint8_t d = rd8(MCU_USB, USB_CSR_DUMMY1);
        if (rd16(MCU_USB, USB_BURST_SIZE) == 0) d &= ~DYNAMIC_BURST; else d |= DYNAMIC_BURST;
        wr8(MCU_USB, USB_CSR_DUMMY1, d);
    } else if (ver == V6) {
        uint8_t d = rd8(MCU_USB, USB_CSR_DUMMY1);
        if (rd16(MCU_USB, USB_BURST_SIZE) == 0) d &= ~DYNAMIC_BURST; else d |= DYNAMIC_BURST;
        wr8(MCU_USB, USB_CSR_DUMMY1, d);
    }
    wr8(MCU_USB, USB_CSR_DUMMY2, rd8(MCU_USB, USB_CSR_DUMMY2) | EP4_FULL_FC);
    wr16(MCU_USB, USB_WDT11_CTRL, rd16(MCU_USB, USB_WDT11_CTRL) & ~(uint32_t)TIMER11_EN);
    wr16(MCU_PLA, PLA_LED_FEATURE, rd16(MCU_PLA, PLA_LED_FEATURE) & ~(uint32_t)LED_MODE_MASK);
    { uint32_t d = FIFO_EMPTY_1FB | ROK_EXIT_LPM; d |= (ver == V4 && !super_speed) ? LPM_TIMER_500MS : LPM_TIMER_500US; wr8(MCU_USB, USB_LPM_CTRL, d); }
    { uint32_t d = rd16(MCU_USB, USB_AFE_CTRL2); d &= ~(uint32_t)SEN_VAL_MASK; d |= SEN_VAL_NORMAL | SEL_RXIDLE; wr16(MCU_USB, USB_AFE_CTRL2, d); }
    wr16(MCU_USB, USB_CONNECT_TIMER, 1);
    power_cut8153(0); enable_fc(); tally_reset();
}
static void init_8153b(void)
{
    aldps8153_off(); u1u2en_b(0);
    wait_bit(MCU_PLA, PLA_BOOT_CTRL, AUTOLOAD_DONE, 1);
    for (int i = 0; i < 2000; i++) { uint16_t s = reg_r(OCP_PHY_STATUS) & PHY_STAT_MASK; if (s == PHY_STAT_LAN_ON || s == PHY_STAT_PWRDN) break; mdelay(1); }
    u2p3en(0);
    wr16(MCU_USB, USB_MSC_TIMER, 0x0fff);
    power_cut8153(0);
    wr16(MCU_PLA, PLA_MAC_PWR_CTRL2, rd16(MCU_PLA, PLA_MAC_PWR_CTRL2) | MAC_CLK_SPDWN_EN);
    wr16(MCU_PLA, PLA_MAC_PWR_CTRL3, rd16(MCU_PLA, PLA_MAC_PWR_CTRL3) & ~(uint32_t)PLA_MCU_SPDWN_EN);
    if (ver == V9 && (rd8(MCU_PLA, 0xdc00) & 0x20)) wr16(MCU_PLA, PLA_PHY_PWR, rd16(MCU_PLA, PLA_PHY_PWR) | TEST_IO_OFF);
    wr16(MCU_USB, USB_USB_CTRL, rd16(MCU_USB, USB_USB_CTRL) & ~(uint32_t)(RX_AGG_DISABLE | RX_ZERO_EN));
    tally_reset();
    /* r8153b_hw_phy_cfg: 펌웨어 패치 제외 */
    { uint16_t d = mdio_r(MII_BMCR); if (d & BMCR_PDOWN) mdio_w(MII_BMCR, d & ~BMCR_PDOWN); }
    wr16(MCU_USB, USB_U1U2_TIMER, 500);
    { uint16_t d = sram_r(SRAM_GREEN_CFG); sram_w(SRAM_GREEN_CFG, d | R_TUNE_EN); }
    reg_w(OCP_NCTL_CFG, reg_r(OCP_NCTL_CFG) | PGA_RETURN_EN);
    { uint32_t e = efuse_read(0x7d); e = ((e & 0x1fff0) >> 1) | (e & 0x7); if (e != 0xffff) reg_w(OCP_ADC_IOFFSET, (uint16_t)e); }
    { uint32_t c = reg_r(0xc426) & 0x3fff; if (c) { uint32_t sw = (16000000 / c) & SAW_CNT_1MS_MASK; uint32_t d = rd16(MCU_USB, USB_UPS_CFG); d = (d & ~(uint32_t)SAW_CNT_1MS_MASK) | sw; wr16(MCU_USB, USB_UPS_CFG, d); } }
    wr16(MCU_PLA, PLA_PHY_PWR, rd16(MCU_PLA, PLA_PHY_PWR) | PFM_PWM_SWITCH);
    enable_fc();
}
static void set_speed_auto(void)
{
    uint16_t anar = mdio_r(MII_ADVERTISE), gbcr = 0;
    anar |= ADV_ALL10_100;
    if (gmii) { gbcr = mdio_r(MII_CTRL1000); gbcr |= ADV_1000; mdio_w(MII_CTRL1000, gbcr); }
    mdio_w(MII_ADVERTISE, anar);
    mdio_w(MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART | BMCR_RESET);
}

/* ---- Nic ---- */
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
        if (!e || e->drv != &nic_r8152) continue;
        uint8_t in = 0, out = 0; uint16_t mo = 512;
        for (uint8_t i = 0; i < ifd.NumEndpoints; i++) {
            EFI_USB_ENDPOINT_DESCRIPTOR ep;
            if (u->UsbGetEndpointDescriptor(u, i, &ep) != EFI_SUCCESS) continue;
            if ((ep.Attributes & 3) != 2) continue;
            if (ep.EndpointAddress & 0x80) { if (!in) in = ep.EndpointAddress; } else if (!out) { out = ep.EndpointAddress; mo = ep.MaxPacketSize & 0x7ff; }
        }
        ulog("NET.usb r8152 vid", dd.IdVendor); ulog("NET.usb r8152 pid", dd.IdProduct);
        ulog("NET.usb r8152 ep_in", in); ulog("NET.usb r8152 ep_out", out); ulog("NET.usb r8152 bcdUSB", dd.BcdUSB);
        if (!in || !out) { net_log("NET.usb r8152 skip why=no_bulk_endpoints"); continue; }
        io = u; ep_in = in; ep_out = out; mps_out = mo ? mo : 512; bcd_usb = dd.BcdUSB; super_speed = bcd_usb >= 0x0300;
        found = 1;
    }
    if (hs) UBS->FreePool(hs);
    return found;
}

/* 엔드포인트 정지 풀기 (CLEAR_FEATURE ENDPOINT_HALT). 칩의 BMU 를 되돌리면 칩 쪽 데이터 토글이 처음으로 가는데, 호스트(xHCI) 쪽 토글은 그대로라 두 번째 시작부터 받은 패킷이 버려졌다 (유선→무선→유선). 펌웨어 USB 버스가 이 요청을 받으면 호스트 쪽 토글도 처음으로 돌려 준다 */
static void clear_halt(uint8_t ep)
{
    EFI_USB_DEVICE_REQUEST rq; uint32_t st = 0;
    rq.RequestType = 0x02; rq.Request = 1; rq.Value = 0; rq.Index = ep; rq.Length = 0;
    EFI_STATUS r = io->UsbControlTransfer(io, &rq, EfiUsbNoData, 500, 0, 0, &st);
    net_log_hex("NET.usb r8152 clear_halt ep", ep); net_log_hex("NET.usb r8152 clear_halt res", (uint32_t)(r & 0xFFFF));
}

/* 받기. 빈 상태에서 UsbBulkTransfer(시간 제한 1ms)를 부르면 시간 초과 처리(전송 취소)에 약 200ms 가 걸려 프레임이 초당 5번으로 느려졌다
 * (lwIP 타이머·주소 충돌 검사가 느려져 DHCP 28초). 먼저 비동기 받기(UsbAsyncInterruptTransfer 를 bulk-in 끝점에)를 시도하고, 안 되면 PLA 의 RX FIFO 비었음 비트를 보고 있을 때만 읽는다 */
#define AQ 8
static struct { volatile int len; uint8_t d[RX_BUF]; } aq[AQ] __attribute__((aligned(64)));
static volatile int a_head, a_tail, a_cnt, a_drop, a_err;
static int a_on, a_slot = -1, link_since;
static uint32_t force_next;
static EFI_STATUS rx_cb(void *data, UINTN len, void *ctx, uint32_t status)
{
    (void)ctx;
    if (status) { a_err++; return EFI_SUCCESS; }
    if (!data || len < RX_DESC) return EFI_SUCCESS;
    if (len > RX_BUF) len = RX_BUF;
    if (aq[a_head].len) { a_drop++; return EFI_SUCCESS; }
    memcpy(aq[a_head].d, data, len);
    aq[a_head].len = (int)len;
    a_head = (a_head + 1) % AQ; a_cnt++;
    return EFI_SUCCESS;
}
static void async_start(void)
{
    a_head = a_tail = a_cnt = a_drop = a_err = 0; a_slot = -1; a_on = 0;
    for (int i = 0; i < AQ; i++) aq[i].len = 0;
    if (!io->UsbAsyncInterruptTransfer) { net_log("NET.usb r8152 rx_mode=poll(no async api)"); return; }
    EFI_STATUS r = io->UsbAsyncInterruptTransfer(io, ep_in, 1, 1, RX_BUF, rx_cb, 0);
    net_log_hex("NET.usb r8152 async_res", (uint32_t)(r & 0xFFFF));
    if (r == EFI_SUCCESS) { a_on = 1; net_log("NET.usb r8152 rx_mode=async"); } else net_log("NET.usb r8152 rx_mode=poll");
}
static void async_stop(void)
{
    if (a_on && io && io->UsbAsyncInterruptTransfer) io->UsbAsyncInterruptTransfer(io, ep_in, 0, 0, 0, 0, 0);
    a_on = 0;
}
static int was_started;
static int probe(void)
{
    if (!do_match()) { net_log("NET.usb r8152 res=none"); return 0; }
    phase = 0; nerr = 0; ocp_base = 0xFFFF; link_up = 0; link_next = 0; rx_len = rx_pos = 0; tx_n = tx_bad = rx_n = rx_bad = 0;
    if (was_started && io->UsbPortReset) {      /* 두 번째 시작부터는 USB 포트를 통째로 초기화 (껐다 켜면 칩이 이전 상태로 남아 송신 bulk 가 시간 초과) */
        EFI_STATUS pr = ((EFI_STATUS (*)(struct EFI_USB_IO_PROTOCOL *))io->UsbPortReset)(io);
        net_log_hex("NET.usb r8152 port_reset res", (uint32_t)(pr & 0xFFFF)); mdelay(300);
    }
    was_started = 1;
    clear_halt(ep_in); clear_halt(ep_out);
    net_log("NET.usb r8152 res=found");
    return 1;
}
static int step(void)
{
    switch (phase) {
    case 0: {
        uint16_t tcr = rd16(MCU_PLA, PLA_TCR1) & VERSION_MASK;
        ver = 0;
        for (unsigned i = 0; i < sizeof vt / sizeof vt[0]; i++) if (vt[i].tcr == tcr) { ver = vt[i].ver; gmii = vt[i].gmii; }
        ulog("NET.usb r8152 tcr", tcr); ulog("NET.usb r8152 ver", (uint32_t)ver);
        if (!ver) { net_log("NET.usb r8152 res=fail why=unknown_version"); return -1; }
        uint8_t m[8] = { 0 };
        if (ocp_read(PLA_IDR, 8, m, MCU_PLA)) { net_log("NET.usb r8152 res=fail why=mac_read"); return -1; }
        memcpy(macaddr, m, 6);
        ulog("NET.usb r8152 mac_hi", macaddr[0] << 16 | macaddr[1] << 8 | macaddr[2]);
        ulog("NET.usb r8152 mac_lo", macaddr[3] << 16 | macaddr[4] << 8 | macaddr[5]);
        phase = 1; return 0;
    }
    case 1:
        if (ver == V1 || ver == V2 || ver == V7) init_8152();
        else if (ver >= V3 && ver <= V6) init_8153();
        else init_8153b();
        net_log("NET.usb r8152 step=init_done"); ulog("NET.usb r8152 ctl_errors", (uint32_t)nerr);
        phase = 2; return 0;
    case 2:
        if (ver == V1 || ver == V2 || ver == V7) { aldps8152(0); exit_oob_8152(); aldps8152(1); }
        else if (ver >= V3 && ver <= V6) { u1u2en(0); aldps8153_off(); first_init_8153(); u2p3en(0); }
        else first_init_8153();
        clear_halt(ep_in); clear_halt(ep_out);
        net_log("NET.usb r8152 step=up_done"); ulog("NET.usb r8152 ctl_errors", (uint32_t)nerr);
        phase = 3; return 0;
    case 3:
        set_speed_auto(); enable();
        ulog("NET.usb r8152 physt", rd8(MCU_PLA, PLA_PHYSTATUS));
        async_start();
        net_log("NET.usb r8152 step=enabled"); ulog("NET.usb r8152 ctl_errors", (uint32_t)nerr);
        phase = 4; return nerr > 20 ? -1 : 1;
    }
    return 1;
}
static void mac(uint8_t o[6]) { memcpy(o, macaddr, 6); }
static int link(void)
{
    uint32_t now = net_now();
    if (phase >= 4 && (int32_t)(now - link_next) >= 0) {
        link_next = now + 500;
        int l = (rd8(MCU_PLA, PLA_PHYSTATUS) & LINK_STATUS) != 0;
        if (l != link_up) { link_up = l; if (l) link_since = (int)now; ulog("NET.usb r8152 physt", rd8(MCU_PLA, PLA_PHYSTATUS)); }
    }
    return link_up;
}
static int tx(const uint8_t *f, int n)
{
    if (n <= 0 || n > 1700) return -1;
    int len = n;
    if (((len + TX_DESC) % mps_out) == 0) len++;            /* 딱 맞으면 짧은 패킷이 없어 전송이 끝나지 않을 수 있음 → 1바이트 덧붙임(이더넷 패딩) */
    uint32_t o1 = (uint32_t)len | (1u << 31) | (1u << 30);
    txb[0] = (uint8_t)o1; txb[1] = (uint8_t)(o1 >> 8); txb[2] = (uint8_t)(o1 >> 16); txb[3] = (uint8_t)(o1 >> 24);
    txb[4] = txb[5] = txb[6] = txb[7] = 0;
    memcpy(txb + TX_DESC, f, (size_t)n);
    if (len > n) txb[TX_DESC + n] = 0;
    UINTN l = (UINTN)len + TX_DESC; uint32_t st = 0;
    EFI_STATUS s = io->UsbBulkTransfer(io, ep_out, txb, &l, 100, &st);
    if (s != EFI_SUCCESS) { if (tx_bad < 4) { net_log_hex("NET.usb r8152 tx_err", (uint32_t)(s & 0xFFFF)); net_log_hex("NET.usb r8152 tx_err st", st); } tx_bad++; return -1; }
    tx_n++; return 0;
}
static int rx_fill(void)                 /* 다음 묶음을 rxb 에 채운다. 1 = 채움 */
{
    if (a_on) {
        if (a_slot >= 0) { aq[a_slot].len = 0; a_slot = -1; }
        if (!aq[a_tail].len) {
            if (link_up && !a_cnt && (int32_t)(net_now() - (uint32_t)link_since) > 8000 && !a_err) {   /* 연결 8초 뒤에도 하나도 못 받음 → 비동기가 안 도는 펌웨어 */
                net_log("NET.usb r8152 async_silent -> poll"); async_stop();
            }
            return 0;
        }
        a_slot = a_tail; a_tail = (a_tail + 1) % AQ;
        memcpy(rxb, aq[a_slot].d, (size_t)aq[a_slot].len); rx_len = aq[a_slot].len; rx_pos = 0;
        aq[a_slot].len = 0; a_slot = -1;
        return 1;
    }
    uint32_t now = net_now();
    int due = (int32_t)(now - force_next) >= 0;                     /* 1초마다는 FIFO 비트와 상관없이 한 번 읽어 봄 */
    if (!due && (rd8(MCU_PLA, PLA_OOB_CTRL) & 0x10)) return 0;       /* RX FIFO 가 비어 있으면 읽지 않는다 (시간 초과 처리가 느림) */
    if (due) force_next = now + 1000;
    UINTN l = RX_BUF; uint32_t st = 0;
    EFI_STATUS s = io->UsbBulkTransfer(io, ep_in, rxb, &l, due ? 1 : 20, &st);
    if (s != EFI_SUCCESS || l < RX_DESC) { if (s != EFI_SUCCESS && s != EFI_TO && rx_bad < 4) { net_log_hex("NET.usb r8152 rx_err", (uint32_t)(s & 0xFFFF)); net_log_hex("NET.usb r8152 rx_err st", st); rx_bad++; } return 0; }
    rx_len = (int)l; rx_pos = 0;
    return 1;
}
static int rx(uint8_t *f, int max)
{
    if (rx_pos >= rx_len) {
        rx_pos = rx_len = 0;
        if (!rx_fill()) return 0;
    }
    if (rx_pos + RX_DESC > rx_len) { rx_pos = rx_len = 0; return 0; }
    uint32_t o1 = rxb[rx_pos] | rxb[rx_pos + 1] << 8 | rxb[rx_pos + 2] << 16 | (uint32_t)rxb[rx_pos + 3] << 24;
    int len = (int)(o1 & 0x7fff);
    if (len < 4 + 14 || rx_pos + RX_DESC + len > rx_len + 4 /* 마지막 조각의 CRC 는 없을 수 있음 */) { rx_pos = rx_len = 0; rx_bad++; return 0; }
    int pk = len - 4;
    if (rx_pos + RX_DESC + pk > rx_len) { rx_pos = rx_len = 0; rx_bad++; return 0; }
    int r = pk > max ? max : pk;
    memcpy(f, rxb + rx_pos + RX_DESC, (size_t)r);
    rx_pos = (rx_pos + RX_DESC + len + 7) & ~7;
    rx_n++;
    return r;
}
static void diag(void)
{
    ulog("NET.usb r8152 tx_ok", (uint32_t)tx_n); ulog("NET.usb r8152 tx_bad", (uint32_t)tx_bad);
    ulog("NET.usb r8152 rx_ok", (uint32_t)rx_n); ulog("NET.usb r8152 rx_bad", (uint32_t)rx_bad);
    ulog("NET.usb r8152 ctl_errors", (uint32_t)nerr);
    ulog("NET.usb r8152 async_on", (uint32_t)a_on); ulog("NET.usb r8152 async_cnt", (uint32_t)a_cnt); ulog("NET.usb r8152 async_drop", (uint32_t)a_drop); ulog("NET.usb r8152 async_err", (uint32_t)a_err);
    ulog("NET.usb r8152 physt", rd8(MCU_PLA, PLA_PHYSTATUS));
    ulog("NET.usb r8152 bmcr", mdio_r(MII_BMCR)); ulog("NET.usb r8152 bmsr", mdio_r(1)); ulog("NET.usb r8152 anar", mdio_r(MII_ADVERTISE));
    ulog("NET.usb r8152 lpa", mdio_r(5)); ulog("NET.usb r8152 gbcr", mdio_r(MII_CTRL1000)); ulog("NET.usb r8152 gbsr", mdio_r(10));
    ulog("NET.usb r8152 phy_stat", reg_r(OCP_PHY_STATUS)); ulog("NET.usb r8152 rcr", rd32(MCU_PLA, PLA_RCR)); ulog("NET.usb r8152 cr", rd8(MCU_PLA, PLA_CR));
}
static void stop(void)
{
    if (!io || phase < 4) return;
    async_stop();
    if (ver >= V3) { if (ver == V8 || ver == V9) rtl_disable(); else { aldps8153_off(); rtl_disable(); bmu_reset(); } }
    else { aldps8152(0); rtl_disable(); aldps8152(1); }
    io = 0; phase = 0;
}
const Nic nic_r8152 = { .name = "RTL8152/8153", .probe = probe, .mac = mac, .link = link, .tx = tx, .rx = rx, .diag = diag, .stop = stop, .step = step, .mtu = 0 };
