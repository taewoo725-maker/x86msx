/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* rtw88(Linux) 코드를 UEFI 에서 그대로 쓰기 위한 얇은 껍데기. rtw88 은 BSD-3-Clause 로 사용 (THIRD-PARTY-NOTICES.txt).
 * m = 칩의 BAR2(메모리 맵 레지스터). 드라이버 구조체는 필요한 칸만 둔다. */
#ifndef RTWSHIM_H
#define RTWSHIM_H
#include <stdbool.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int8_t s8;
#define BIT(n) (1u << (n))
#define GENMASK(h, l) (((~0u) >> (31 - (h))) & ((~0u) << (l)))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define RFREG_MASK 0xfffff
#define MASKBYTE0 0x000000ffu
#define MASKLWORD 0x0000ffffu
#define MASKHWORD 0xffff0000u
#define MASKDWORD 0xffffffffu
#define EFUSE_READ_FAIL 0xff
enum { RF_PATH_A = 0, RF_PATH_B = 1, RTW_RF_PATH_MAX = 2 };
#define DACK_PATH_8822C 2
#define DACK_REG_8822C 16
#define DACK_RF_8822C 1
#define DACK_SN_8822C 100
#define DACK_MSBK_BACKUP_NUM 0xf
#define DACK_DCK_BACKUP_NUM 0x2
#define REG_DIS_DPD 0x0a70
#define DIS_DPD_MASK GENMASK(9, 0)
#define DIS_DPD_RATEALL GENMASK(9, 0)
#define REG_DCKA_I_0 0x18bc
#define REG_DCKA_I_1 0x18c0
#define REG_DCKA_Q_0 0x18d8
#define REG_DCKA_Q_1 0x18dc
#define REG_DCKB_I_0 0x41bc
#define REG_DCKB_I_1 0x41c0
#define REG_DCKB_Q_0 0x41d8
#define REG_DCKB_Q_1 0x41dc
#define RF_PA 0x60
#define RF_PABIAS_2G_MASK GENMASK(15, 12)
#define RF_PABIAS_5G_MASK GENMASK(19, 16)
#define RF_THEMAL_MASK GENMASK(19, 16)
#define PPG_THERMAL_A 0x1ef
#define PPG_THERMAL_B 0x1b0
#define PPG_2GH_TXAB 0x1d2
#define PPG_2GL_TXAB 0x1d4
#define PPG_2GM_TXAB 0x1ee
#define PPG_2G_A_MASK GENMASK(3, 0)
#define PPG_2G_B_MASK GENMASK(7, 4)
#define PPG_PABIAS_2GB 0x1d5
#define PPG_PABIAS_2GA 0x1d6
#define PPG_PABIAS_5GB 0x1d7
#define PPG_PABIAS_5GA 0x1d8
#define PPG_PABIAS_MASK GENMASK(3, 0)
#define PPG_5G_MASK GENMASK(4, 0)
#define PPG_5GH1_TXB 0x1db
#define PPG_5GH1_TXA 0x1dc
#define PPG_5GM2_TXB 0x1df
#define PPG_5GM2_TXA 0x1e0
#define PPG_5GM1_TXB 0x1e3
#define PPG_5GM1_TXA 0x1e4
#define PPG_5GL2_TXB 0x1e7
#define PPG_5GL2_TXA 0x1e8
#define PPG_5GL1_TXB 0x1eb
#define PPG_5GL1_TXA 0x1ec
struct rtw_backup_info { u8 len; u32 reg; u32 val; };
struct rtw_dm_info { u32 dack_adck[RTW_RF_PATH_MAX]; u16 dack_msbk[RTW_RF_PATH_MAX][2][DACK_MSBK_BACKUP_NUM]; u8 dack_dck[RTW_RF_PATH_MAX][2][DACK_DCK_BACKUP_NUM]; };
struct rtw_hal_s { int rf_path_num; };
struct rtw_dev { volatile uint8_t *m; struct rtw_hal_s hal; struct rtw_dm_info dm_info; int nerr; const char *err1; };
extern const uint8_t *rtw_ef_phy_p;
static inline int shim_ffs(u32 x) { int s = 0; while (!((x >> s) & 1)) s++; return s; }
#define FIELD_GET(mask, v) (((v) & (mask)) >> shim_ffs(mask))
#define FIELD_PREP(mask, v) (((u32)(v) << shim_ffs(mask)) & (mask))
#define rtw_dbg(...) do { } while (0)
#define RTW_DBG_RFK 0
#define rtw_err(d, ...) do { (d)->nerr++; if (!(d)->err1) (d)->err1 = "rtw_err"; } while (0)
#define WARN_ON(c) ((c) ? ((void)0, 1) : 0)
static inline u8 rtw_read8(struct rtw_dev *d, u32 a) { return d->m[a]; }
#define rtw_chip_wcpu_8051(d) 0   /* 8822C 는 3081 CPU */
static inline u32 rtw_read32(struct rtw_dev *d, u32 a) { return *(volatile u32 *)(d->m + a); }
static inline void rtw_write32(struct rtw_dev *d, u32 a, u32 v) { *(volatile u32 *)(d->m + a) = v; }
static inline void rtw_write8(struct rtw_dev *d, u32 a, u8 v) { d->m[a] = v; }
static inline void rtw_write16(struct rtw_dev *d, u32 a, u16 v) { *(volatile u16 *)(d->m + a) = v; }
static inline u32 rtw_read32_mask(struct rtw_dev *d, u32 a, u32 mask) { return (rtw_read32(d, a) & mask) >> shim_ffs(mask); }
static inline void rtw_write32_mask(struct rtw_dev *d, u32 a, u32 mask, u32 v) { u32 o = rtw_read32(d, a); rtw_write32(d, a, (o & ~mask) | ((v << shim_ffs(mask)) & mask)); }
static inline void rtw_write32_set(struct rtw_dev *d, u32 a, u32 b) { rtw_write32(d, a, rtw_read32(d, a) | b); }
static inline void rtw_write32_clr(struct rtw_dev *d, u32 a, u32 b) { rtw_write32(d, a, rtw_read32(d, a) & ~b); }
static inline void rtw_restore_reg(struct rtw_dev *d, struct rtw_backup_info *b, u32 n)
{ for (u32 i = 0; i < n; i++) { if (b[i].len == 1) rtw_write8(d, b[i].reg, (u8)b[i].val); else if (b[i].len == 2) rtw_write16(d, b[i].reg, (u16)b[i].val); else rtw_write32(d, b[i].reg, b[i].val); } }
/* RF 레지스터: 주소 0 이외는 직접 접근(0x3C00/0x4C00 + 주소*4), 주소 0 쓰기는 SIPI(0x1808/0x4108) */
static inline u32 rtw_read_rf(struct rtw_dev *d, int p, u32 addr, u32 mask) { return rtw_read32_mask(d, (p ? 0x4C00u : 0x3C00u) + ((addr & 0xff) << 2), mask & RFREG_MASK); }
static inline void rtw_write_rf(struct rtw_dev *d, int p, u32 addr, u32 mask, u32 data)
{
    addr &= 0xff; mask &= RFREG_MASK;
    if (addr) { rtw_write32_mask(d, (p ? 0x4C00u : 0x3C00u) + (addr << 2), mask, data); shim_us(1); return; }
    if (mask != RFREG_MASK) { u32 o = rtw_read_rf(d, p, 0, RFREG_MASK); data = (o & ~mask) | (data << shim_ffs(mask)); }
    rtw_write32(d, p ? 0x4108 : 0x1808, ((addr << 20) | (data & 0xfffff)) & 0x0fffffff); shim_us(13);
}
#define swap(a, b) do { __typeof__(a) _t = (a); (a) = (b); (b) = _t; } while (0)
/* 리눅스 util.c check_hw_ready: 50마이크로초 간격으로 최대 50밀리초 기다림 (마스크 읽기 값이 target 과 같아질 때까지) */
static inline bool check_hw_ready(struct rtw_dev *d, u32 addr, u32 mask, u32 target)
{ for (int i = 0; i < 1000; i++) { if (rtw_read32_mask(d, addr, mask) == target) return true; shim_us(50); } return rtw_read32_mask(d, addr, mask) == target; }
static inline void mdelay(int n) { while (n-- > 0) shim_us(1000); }
static inline void rtw_read8_physical_efuse(struct rtw_dev *d, u32 a, u8 *v) { (void)d; *v = a < 512 ? rtw_ef_phy_p[a] : EFUSE_READ_FAIL; }
#endif
