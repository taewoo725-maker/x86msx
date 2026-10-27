/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* rtwdet.c - RTL8822CE 식별 레지스터 읽기 (설명 rtwdet.h). MMIO 범위: BAR2 + 0x000~0x1FF 중 4곳만
 * rtwdet_power() 1단계 — 칩 전원 켜기. 순서·값은 리눅스 rtw88(rtw8822c.c 의 카드 켜기 표, mac.c 의 파서,
 *   라이선스 GPL-2.0 OR BSD-3-Clause 중 BSD-3-Clause)을 참고해 다시 썼다. 원문 소스는 넣지 않았다.
 *   Copyright(c) 2018-2019 Realtek Corporation (참고한 표의 원저작권) */
#include "rtwdet.h"
#include <stdint.h>

static inline void p_outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(p)); }
static inline uint32_t p_inl(uint16_t p) { uint32_t v; __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static inline void p_outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static uint32_t cr(int b, int d, int f, int off)
{
    p_outl(0xCF8, 0x80000000u | (uint32_t)(b << 16) | (uint32_t)(d << 11) | (uint32_t)(f << 8) | (uint32_t)(off & 0xFC));
    return p_inl(0xCFC);
}
static void cw(int b, int d, int f, int off, uint32_t v)
{
    p_outl(0xCF8, 0x80000000u | (uint32_t)(b << 16) | (uint32_t)(d << 11) | (uint32_t)(f << 8) | (uint32_t)(off & 0xFC));
    p_outl(0xCFC, v);
}
static void wait_ms(int ms) { for (int i = 0; i < ms * 1000; i++) p_outb(0x80, 0); }   /* 포트 0x80 쓰기 ≈ 1µs */
static void hx(char *o, uint32_t v, int digits)
{
    static const char H[] = "0123456789ABCDEF";
    for (int i = 0; i < digits; i++) o[i] = H[(v >> ((digits - 1 - i) * 4)) & 15];
}
static int put(char *l, const char *t)
{
    int n = 0; while (*t) l[n++] = *t++; l[n] = 0; return n;
}

static char out[8][64];
static int nout, done;

static void run(void)
{
    done = 1;
    int B = -1, D = 0;
    for (int b = 0; b < 16 && B < 0; b++)
        for (int d = 0; d < 32; d++)
            if (cr(b, d, 0, 0) == 0xC82210ECu) { B = b; D = d; break; }
    if (B < 0) { put(out[nout++], "wifi rtw: C822 not found"); return; }

    uint32_t cmd0 = cr(B, D, 0, 4) & 0xFFFFu;
    /* PM capability 찾기 (최대 16개) */
    int pm = 0;
    if (cr(B, D, 0, 4) & 0x00100000u) {
        int p = (int)(cr(B, D, 0, 0x34) & 0xFC);
        for (int i = 0; i < 16 && p >= 0x40; i++) {
            uint32_t h = cr(B, D, 0, p);
            if ((h & 0xFF) == 0x01) { pm = p; break; }
            p = (int)((h >> 8) & 0xFC);
        }
    }
    uint32_t pmcsr0 = pm ? (cr(B, D, 0, pm + 4) & 0xFFFFu) : 0;
    int dst0 = pm ? (int)(pmcsr0 & 3) : -1;
    {
        char *l = out[nout++]; int n = put(l, "wifi rtw pm cap=");
        hx(l + n, (uint32_t)pm, 2); n += 2; n += put(l + n, " D");
        l[n++] = dst0 < 0 ? '?' : (char)('0' + dst0); l[n] = 0;
    }
    if (dst0 > 0) {                                            /* D1~D3 → D0 (복원은 맨 끝) */
        cw(B, D, 0, pm + 4, (pmcsr0 & ~0x8003u & 0xFFFFu));
        wait_ms(10);
    }
    uint32_t bar2 = cr(B, D, 0, 0x18), bar3 = cr(B, D, 0, 0x1C);
    cw(B, D, 0, 4, cmd0 | 0x0006u);                          /* MEM + BM 켜기 (상위 16비트 = 상태: 0 기록) */
    wait_ms(10);
    uint32_t cmd1 = cr(B, D, 0, 4) & 0xFFFFu;
    {
        char *l = out[nout++]; int n = put(l, "wifi rtw cmd ");
        hx(l + n, cmd0, 4); n += 4; n += put(l + n, "->"); hx(l + n, cmd1, 4); n += 4; l[n] = 0;
    }
    if ((bar2 & 1) || bar3 != 0 || !(bar2 & 0xFFFFFFF0u)) {
        put(out[nout++], "wifi rtw: BAR2 not usable (hi!=0/IO)");
    } else {
        volatile uint32_t *mm = (volatile uint32_t *)(uintptr_t)(bar2 & 0xFFFFFFF0u);
        uint32_t f0 = mm[0xF0 / 4], f4 = mm[0xF4 / 4], f8 = mm[0xF8 / 4], c0 = mm[0x100 / 4];
        char *l = out[nout++]; int n = put(l, "wifi rtw F0=");
        hx(l + n, f0, 8); n += 8; n += put(l + n, " F4="); hx(l + n, f4, 8); n += 8; l[n] = 0;
        l = out[nout++]; n = put(l, "wifi rtw F8="); hx(l + n, f8, 8); n += 8;
        n += put(l + n, " CR="); hx(l + n, c0, 8); n += 8; l[n] = 0;
        if (f0 == 0xFFFFFFFFu) put(out[nout++], "wifi rtw: all FF (device off / D3?)");
        else {
            l = out[nout++]; n = put(l, "wifi rtw vendor=");
            hx(l + n, (f0 >> 16) & 15, 1); n += 1; n += put(l + n, " chipver=");
            hx(l + n, (f0 >> 12) & 15, 1); n += 1; l[n] = 0;
        }
    }
    cw(B, D, 0, 4, cmd0);                                     /* 원상 복구 */
    if (dst0 > 0) cw(B, D, 0, pm + 4, (pmcsr0 & ~0x8000u) & 0xFFFFu);
    put(out[nout++], "wifi rtw: restored cmd/pm");
}

int rtwdet_info(char (*lines)[64], int max)
{
    if (!done) run();
    int k = 0;
    for (int i = 0; i < nout && k < max; i++, k++) { int n = 0; while (out[i][n]) { lines[k][n] = out[i][n]; n++; } lines[k][n] = 0; }
    return k;
}

/* ====== 5.0.132: 1단계 - 칩 전원 켜기 (사용자가 DRV\\RTW.INI 에 POWER=1 을 둔 1번 PC에서만 호출됨) ======
 * 하는 일: PCI 메모리·버스마스터 켜기 → 레지스터 응답 확인 → 전원 켜기 표(카드 끔→카드 준비→동작) 실행 → 결과 기록.
 * 하지 않는 일: 펌웨어 올리기, DMA, 안테나·무선 동작. 끝나면 PCI CMD를 처음 값으로 돌려놓는다 (칩 전원은 켠 채로 둠).
 * 로그 줄 형식: "WIFI.<단계> res=ok|fail key=값 ..." (공용 로그 규약) */
typedef struct { uint16_t off; uint8_t cmd, mask, val; } PwrCmd;   /* cmd: 1 쓰기(읽고-바꾸고-쓰기) 2 기다리기 */
static const PwrCmd pwr_on[] = {
    /* 카드 끔 → 카드 준비 (PCI 에 해당하는 줄만) */
    {0x002E, 1, 0x04, 0x04}, {0x002D, 1, 0x01, 0x00}, {0x007F, 1, 0x80, 0x00}, {0x0005, 1, 0x98, 0x00},
    /* 카드 준비 → 동작 */
    {0x0005, 1, 0x1C, 0x00}, {0x0075, 1, 0x01, 0x01}, {0x0006, 2, 0x02, 0x02}, {0x0075, 1, 0x01, 0x00},
    {0x002E, 1, 0x08, 0x00}, {0x0006, 1, 0x01, 0x01}, {0x0005, 1, 0x18, 0x00}, {0x1018, 1, 0x04, 0x04},
    {0x0005, 1, 0x01, 0x01}, {0x0005, 2, 0x01, 0x00}, {0x0074, 1, 0x20, 0x20}, {0x0071, 1, 0x10, 0x00},
    {0x0062, 1, 0xE0, 0xE0}, {0x0061, 1, 0xE0, 0x00}, {0x001F, 1, 0xC0, 0x80}, {0x00EF, 1, 0xC0, 0x80},
    {0x1045, 1, 0x10, 0x10}, {0x0010, 1, 0x04, 0x04}, {0x1064, 1, 0x02, 0x02},
};
static char *lg; static int lgn, lgmax;
static void lput(const char *t) { while (*t && lgn < lgmax - 1) lg[lgn++] = *t++; lg[lgn] = 0; }
static void lhex(uint32_t v, int d) { char t[9]; hx(t, v, d); t[d] = 0; lput(t); }
static void lkv(const char *k, uint32_t v, int d) { lput(" "); lput(k); lput("="); lhex(v, d); }


/* ====== 5.0.136: 2b-2단계 - 펌웨어 올리기 (rtw88 mac.c __rtw_download_firmware / fw.c rtw_fw_write_data_rsvd_page 순서) ======
 * 한 덩어리(최대 4096바이트)씩: ① 신호등 큐로 칩 내부 예약 페이지 0 에 복사 ② 칩 내부 DDMA 로 IMEM/DMEM/EMEM 에 옮기며 검사합 확인.
 * 리눅스와 다른 점: BT 공존 레지스터(ltecoex 0x38) 백업/복원은 생략. PCIe 라서 TX 서술자 검사합은 쓰지 않음(리눅스도 동일). */
static int g_B, g_D, g_pcie;
#define R8(o)  (m[(o)])
#define R16(o) (*(volatile uint16_t *)(m + (o)))
#define R32(o) (*(volatile uint32_t *)(m + (o)))
static void us(int n) { for (int i = 0; i < n; i++) p_outb(0x80, 0); }
static int pollm(volatile uint8_t *m, uint32_t off, uint32_t mask, uint32_t want, int max_us)
{ for (int t = 0; t < max_us / 10; t++) { if ((R32(off) & mask) == want) return 1; us(10); } return 0; }
static void dl_cpu(volatile uint8_t *m, int en)
{
    if (en) { R8(0x1D) |= 1; R8(0x03) |= 4; }
    else    { R8(0x03) &= (uint8_t)~4; R8(0x1D) &= (uint8_t)~1; }
}
/* 예약 페이지 0 에 chunk 쓰기. ring=BD 쪽 메모리(= DESA 주소), pk=패킷 메모리(48바이트 서술자 + 데이터) */
static int dl_rsvd(volatile uint8_t *m, uint8_t *ring, uint8_t *pk, const uint8_t *d, uint32_t n)
{
    uint8_t bc = R8(0x550), b101 = R8(0x101), b422 = R8(0x422);
    int ok;
    R16(0x204) = (uint16_t)(0x8000 | 0);                         /* pg_addr(0) | BCN_VALID */
    R8(0x101) = (uint8_t)(b101 | 1);                             /* ENSWBCN */
    R8(0x550) = (uint8_t)((bc & ~0x08) | 0x10);                  /* BCN 기능 끄기, TSF 갱신 막기 */
    R8(0x422) = (uint8_t)(b422 & ~0x40);                         /* EN_BCNQ_DL 끄기 */
    volatile uint32_t *w = (volatile uint32_t *)pk;
    for (int i = 0; i < 12; i++) w[i] = 0;
    uint32_t bmc = d[4] & 1;
    w[0] = n | (48u << 16) | (bmc << 24) | (1u << 26) | (1u << 31);
    w[1] = (16u << 8) | (7u << 16);                              /* QSEL=BEACON, RATE_ID=G */
    w[3] = (1u << 8) | (1u << 10);                               /* USE_RATE, DISDATAFB */
    w[4] = 4;                                                    /* 6M */
    w[8] = 1u << 15;                                             /* EN_HWSEQ */
    volatile uint8_t *pd = pk + 48; for (uint32_t i = 0; i < n; i++) pd[i] = d[i];
    uint32_t pa = (uint32_t)(uintptr_t)pk;
    volatile uint16_t *bd = (volatile uint16_t *)ring;           /* BD[0]=서술자 48바이트, BD[1]=데이터 */
    volatile uint32_t *bd32 = (volatile uint32_t *)ring;
    uint32_t tot = n + 48;
    bd[0] = 48; bd[1] = (uint16_t)(((tot - 1) / 128 + 1) | 0x8000); bd32[1] = pa;
    bd[4] = (uint16_t)n; bd[5] = 0; bd32[3] = pa + 48;
    __asm__ volatile("mfence" ::: "memory");
    R8(0x383) |= 0x10;                                           /* BCNQ_FLAG: 신호등 큐 시작 */
    ok = pollm(m, 0x204, 0x8000, 0x8000, 100000);
    if (!ok) {
        lput("WIFI.diag"); lkv("r204", R32(0x204), 8); lkv("r300", R32(0x300), 8); lkv("r308", R32(0x308), 8); lkv("r380", R32(0x380), 8); lkv("r39C", R32(0x39C), 8);
        lkv("r420", R32(0x420), 8); lkv("r550", R32(0x550), 8); lkv("r100", R32(0x100), 8); lkv("r10C", R32(0x10C), 8); lkv("r230", R32(0x230), 8); lkv("r22C", R32(0x22C), 8);
        lkv("bd0", ((volatile uint32_t *)ring)[0], 8); lkv("bd1", ((volatile uint32_t *)ring)[1], 8); lkv("bd2", ((volatile uint32_t *)ring)[2], 8); lkv("bd3", ((volatile uint32_t *)ring)[3], 8);
        lkv("d0", ((volatile uint32_t *)pk)[0], 8); lkv("d1", ((volatile uint32_t *)pk)[1], 8); lput("\r\n");
        lput("WIFI.pci"); lkv("cmdsts", cr(g_B, g_D, 0, 4), 8);
        if (g_pcie) { lkv("devctl_sta", cr(g_B, g_D, 0, g_pcie + 8), 8); lkv("lnkctl_sta", cr(g_B, g_D, 0, g_pcie + 0x10), 8); }
        lkv("r210", R32(0x210), 8); lkv("r1330", R32(0x1330), 8); lput("\r\n");
        /* 재시도 - DMA 인터페이스 리셋 비트를 풀고 포인터를 다시 비운 뒤 한 번 더 */
        R32(0x300) &= ~(1u << 20); us(100); R32(0x39C) = 0xFFFFFFFFu; us(100);
        R16(0x204) = 0x8000; R8(0x383) |= 0x10;
        ok = pollm(m, 0x204, 0x8000, 0x8000, 100000);
        lput("WIFI.retry1 res="); lput(ok ? "ok" : "fail"); lkv("r204", R32(0x204), 8); lkv("r300", R32(0x300), 8); lkv("r380", R32(0x380), 8); lput("\r\n");
    }
    R16(0x204) = 0x8000;                                         /* 리눅스: rsvd_boundary(아직 0) | BCN_VALID */
    R8(0x550) = bc; R8(0x422) = b422; R8(0x101) = b101;
    return ok;
}
/* 한 구역(IMEM/DMEM/EMEM)을 칩 메모리로. src=0 고정 */
static int dl_seg(volatile uint8_t *m, uint8_t *ring, uint8_t *pk, const uint8_t *d, uint32_t dst, uint32_t size, const char **why)
{
    uint32_t off = 0, first = 1;
    R32(0x1208) |= 1u << 25;                                     /* RESET_CHKSUM_STS */
    while (size) {
        uint32_t n = size >= 0x1000 ? 0x1000 : size;
        if (!dl_rsvd(m, ring, pk, d + off, n)) { *why = "rsvd_bcn_valid"; return 0; }
        if (!pollm(m, 0x1208, 1u << 31, 0, 100000)) { *why = "ddma_busy1"; return 0; }
        uint32_t ctl = (1u << 29) | (1u << 31) | (n & 0x3FFFF);
        if (!first) ctl |= 1u << 24;
        R32(0x1200) = 0x18780000u + 0 + 48; R32(0x1204) = dst + off; R32(0x1208) = ctl;
        if (!pollm(m, 0x1208, 1u << 31, 0, 100000)) { *why = "ddma_done"; return 0; }
        first = 0; off += n; size -= n;
    }
    uint8_t fc = R8(0x80);
    int bad = (R32(0x1208) & (1u << 27)) != 0;
    if (dst < 0x200000u) fc |= bad ? 0x08 : 0x18; else fc |= bad ? 0x20 : 0x60;
    if (dst < 0x200000u) { if (bad) fc &= (uint8_t)~0x10; } else { if (bad) fc &= (uint8_t)~0x40; }
    R8(0x80) = fc;
    if (bad) { *why = "checksum"; return 0; }
    return 1;
}

/* 3a단계 - rtw_init_trx_cfg (rtw88 mac.c: txdma_queue_mapping, priority_queue_cfg, init_h2c). 8822C/PCIe 값으로 계산해 둠:
 * 큐 매핑표[1]={VO NORMAL,VI NORMAL,BE LOW,BK LOW,MG EXTRA,HI HIGH}, 페이지표[1]={64,64,64,64,1}, 전송FIFO 2048쪽, 예약 110쪽(경계 1938), 공용 1681쪽 */
static int rtw_trxcfg(volatile uint8_t *m)
{
    R16(0x10C) = 0xC5A0;                                          /* TXDMA_PQ_MAP */
    R8(0x100) = 0; R8(0x100) = 0xFF;                              /* CR: 송수신 엔진 켜기 */
    R32(0x1330) = 1u << 31;                                       /* H2CQ_CSR */
    R16(0x230) = 64; R16(0x234) = 64; R16(0x238) = 64; R16(0x23C) = 64; R16(0x240) = 1681;
    R32(0x22C) |= 1u << 31;                                       /* LD_RQPN */
    R16(0x204) = 1938; R8(0x422) |= 0x10;                         /* rsvd boundary, EN_WR_FREE_TAIL */
    R16(0x424) = 1938; R16(0x206) = 1938; R16(0x456) = 1938;
    R32(0x11C) = 24576 - 256 - 1;                                 /* RXFF_BNDY */
    R8(0x208) |= 1;                                               /* AUTO_INIT_LLT */
    int llt = pollm(m, 0x208, 1, 0, 100000);
    R8(0x103) = 0;
    uint32_t addr = 1986u << 7, size = 8u << 7;                   /* H2C 큐 위치 (예약 쪽 1986) */
    R32(0x244) = (R32(0x244) & 0xFFFC0000u) | addr;
    R32(0x24C) = (R32(0x24C) & 0xFFFC0000u) | addr;
    R32(0x248) = (R32(0x248) & 0xFFFC0000u) | (addr + size);
    R8(0x254) = (uint8_t)((R8(0x254) & 0xFC) | 1);
    R8(0x254) = (uint8_t)((R8(0x254) & 0xFB) | 4);
    R8(0x20D) = (uint8_t)((R8(0x20D) & 0x7F) | 0x80);
    uint32_t wp = R32(0x10D4) & 0x3FFFF, rp = R32(0x10D0) & 0x3FFFF;
    uint32_t fr = wp >= rp ? size - (wp - rp) : rp - wp;
    int ok = llt && fr == size;
    lput("WIFI.trx res="); lput(ok ? "ok" : "fail"); lkv("llt", (uint32_t)llt, 1); lkv("h2c_free", fr, 4); lkv("size", size, 4);
    lkv("r100", R32(0x100), 8); lkv("r10C", R16(0x10C), 4); lkv("r204", R32(0x204), 8); lkv("r11C", R32(0x11C), 8); lkv("r240", R16(0x240), 4);
    lkv("r244", R32(0x244), 8); lkv("r248", R32(0x248), 8); lkv("r24C", R32(0x24C), 8); lkv("r254", R8(0x254), 2); lput("\r\n");
    return ok;
}

/* 3b단계 - rtw8822c_mac_init + rtw_drv_info_cfg + PCIe interface_cfg. 순서·값은 rtw88 rtw8822c.c 의 해당 함수를 컴파일러로 풀어 그대로 옮김.
 * 쓴 값이 칩에서 같게 읽히는지 세어서 로그에 남김(일부 레지스터는 다르게 읽히는 것이 정상일 수 있음) */
static int mm_n, mm_first; static uint32_t mm_exp, mm_got;
#define MMNOTE(a, e, g) do { if ((e) != (g)) { if (!mm_n) { mm_first = (a); mm_exp = (e); mm_got = (g); } mm_n++; } } while (0)
#define W8c(a, v)  do { R8(a) = (uint8_t)(v); MMNOTE(a, (uint32_t)(uint8_t)(v), (uint32_t)R8(a)); } while (0)
#define W16c(a, v) do { R16(a) = (uint16_t)(v); MMNOTE(a, (uint32_t)(uint16_t)(v), (uint32_t)R16(a)); } while (0)
#define W32c(a, v) do { R32(a) = (uint32_t)(v); MMNOTE(a, (uint32_t)(v), R32(a)); } while (0)
static int rtw_macinit(volatile uint8_t *m, int cut)
{
    mm_n = 0;
    R8(0x420) |= 0x80;   /* FWHW_TXQ_CTRL */
    W8c(0x0421, 0x0000001F);
    W16c(0x0428, 0x0000100A);
    W32c(0x0514, 0x100A0E0Au);
    W16c(0x063C, 0x00000A0A);
    W16c(0x063E, 0x00000E0E);
    W32c(0x0430, 0x01000000u);
    W32c(0x0434, 0x08070504u);
    W32c(0x043C, 0x08070504u);
    W32c(0x0444, 0xFE01F010u);
    W32c(0x0448, 0x40000000u);
    W32c(0x044C, 0x003FF010u);
    W32c(0x0450, 0x40000000u);
    W32c(0x049C, 0x0600F010u);
    W32c(0x04A0, 0x400003E0u);
    W32c(0x04A4, 0x0600F015u);
    W32c(0x04A8, 0x000000E0u);
    W8c(0x0455, 0x00000070);
    R8(0x045E) |= 0x00000004;
    W8c(0x04E5, 0x000000E0);
    W8c(0x04E6, 0x00000009);
    W32c(0x04C8, 0x3F3F08FFu);
    W16c(0x04CE, 0x00000801);
    W8c(0x1448, 0x00000006);
    W8c(0x144A, 0x00000006);
    W8c(0x144C, 0x00000006);
    W8c(0x144E, 0x00000006);
    R8(0x0426) &= (uint8_t)~0x00000020;
    R32(0x0440) &= ~0x00600000u;
    W32c(0x0500, 0x002FA226u);
    W32c(0x0504, 0x005EA328u);
    W32c(0x0508, 0x005EA42Bu);
    W32c(0x050C, 0x0000A44Fu);
    W8c(0x0512, 0x0000001C);
    R8(0x0521) &= (uint8_t)~0x00000010;
    R8(0x0525) |= 0x00000007;
    R32(0x0024) &= ~0x00300000u;
    W8c(0x055C, 0x00000050);
    W8c(0x0638, 0x00000050);
    R8(0x0577) |= 0x0000000B;
    R8(0x05B4) &= (uint8_t)~0x00000070;
    W16c(0x0522, 0x00000000);
    W8c(0x051B, 0x00000009);
    W32c(0x0544, 0x001B0005u);
    W16c(0x055E, 0x00003030);
    R8(0x0550) |= 0x00000008;
    W32c(0x0540, 0x00006404u);
    W8c(0x0558, 0x00000004);
    W8c(0x0551, 0x00000010);
    W8c(0x0559, 0x00000002);
    W8c(0x055D, 0x000000FF);
    W32c(0x0620, 0xFFFFFFFFu);
    W32c(0x0624, 0xFFFFFFFFu);
    W8c(0x06DE, 0x00000084);
    W8c(0x0640, 0x00000021);
    W8c(0x0639, 0x0000006A);
    W16c(0x0642, 0x00000040);
    W8c(0x0652, 0x000000C8);
    W8c(0x066E, 0x00000005);
    W16c(0x06A0, 0x0000FFFF);
    W16c(0x06A2, 0x0000FFFF);
    W16c(0x06A4, 0x0000FFFF);
    W32c(0x0608, 0xE400220Eu);
    W8c(0x060C, 0x00000018);
    W8c(0x0606, 0x00000030);
    W8c(0x0605, 0x00000030);
    R32(0x1664) |= 0x00000200u;
    W32c(0x07D8, 0xB0810041u);
    W8c(0x07D4, 0x00000098);
    R16(0x1612) = (uint16_t)((R16(0x1612) & 0xF00F) | 0x0550);
    W16c(0x1610, 0x00003F80);
    W32c(0x1614, 0xFFFFFFFFu);
    R16(0x1610) = (uint16_t)(((R16(0x1610) & ~0x70) & ~7) | 7);
    R8(0x0718) |= 0x00000040;
    W32c(0x0304, 0x33330000u);
    /* rtw_drv_info_cfg */
    R8(0x60F) = 4;
    R8(0x115) = (uint8_t)((R8(0x115) & 0xF0) | 0xF);
    R32(0x608) |= 0x10000000u;
    R32(0x7D4) &= ~((1u << 8) | (1u << 9));
    /* PCIe interface_cfg (8822C 컷 D 이상) */
    if (cut >= 3) R32(0x3FC) |= 1u << 26;
    lput("WIFI.mac2 res=ok"); lkv("cut", (uint32_t)cut, 1); lkv("mismatch", (uint32_t)mm_n, 2);
    if (mm_n) { lkv("first", (uint32_t)mm_first, 4); lkv("exp", mm_exp, 8); lkv("got", mm_got, 8); }
    lkv("r608", R32(0x608), 8); lkv("r1610", R16(0x1610), 4); lkv("r1612", R16(0x1612), 4); lkv("r420", R8(0x420), 2); lkv("r550", R8(0x550), 2); lkv("r100", R32(0x100), 8); lput("\r\n");
    return 1;
}

/* 3c단계 - 칩 내부 퓨즈(efuse) 읽기 (rtw88 efuse.c: rtw_dump_physical_efuse_map / rtw_dump_logical_efuse_map, 8822C 지도: 물리 512, 논리 768, 보호 124)
 * 결과: MAC 주소(논리 0x120), RFE 옵션(0xCA), 보드 옵션(0xB?) 등. PHY 표를 고를 때 RFE 옵션이 필요함 */
static uint8_t ef_phy[512], ef_log[768];
static int rtw_efuse(volatile uint8_t *m)
{
    for (int i = 0; i < 768; i++) ef_log[i] = 0xFF;
    R32(0x34) &= ~(3u << 8);                                      /* 뱅크 0 (WiFi) */
    R8(0x29) &= (uint8_t)~1;                                      /* 2.5V LDO 끄기 */
    uint32_t ctl = R32(0x30);
    for (uint32_t a = 0; a < 512; a++) {
        ctl &= ~(0xFFu | (0x3FFu << 8));
        ctl |= (a & 0x3FF) << 8;
        R32(0x30) = ctl & ~(1u << 31);
        int ok = 0;
        for (int t = 0; t < 100000; t++) { us(1); ctl = R32(0x30); if (ctl & (1u << 31)) { ok = 1; break; } }
        if (!ok) { lput("WIFI.efuse res=fail why=timeout"); lkv("addr", a, 4); lput("\r\n"); return 0; }
        ef_phy[a] = (uint8_t)(ctl & 0xFF);
    }
    int pi = 0, err = 0; const int psz = 512 - 124;
    while (pi < psz) {
        uint8_t h1 = ef_phy[pi], h2 = ef_phy[pi + 1], we; int blk;
        if (h1 == 0xFF || ((h1 & 0x1F) == 0xF && h2 == 0xFF)) break;
        if ((h1 & 0x1F) == 0xF) { blk = ((h2 & 0xF0) >> 1) | ((h1 >> 5) & 7); we = h2 & 0xF; pi += 2; }
        else { blk = (h1 & 0xF0) >> 4; we = h1 & 0xF; pi += 1; }
        for (int i = 0; i < 4; i++) {
            if (we & (1 << i)) continue;
            int li = (blk << 3) + (i << 1);
            if (pi + 1 > psz || li + 1 > 768) { err = 1; break; }
            ef_log[li] = ef_phy[pi]; ef_log[li + 1] = ef_phy[pi + 1]; pi += 2;
        }
        if (err) break;
    }
    lput("WIFI.efuse res="); lput(err ? "fail" : "ok"); lkv("phyused", (uint32_t)pi, 3); lkv("rtlid", (uint32_t)(ef_log[0] | ef_log[1] << 8), 4);
    lput(" mac=");
    for (int i = 0; i < 6; i++) { lhex(ef_log[0x120 + i], 2); if (i < 5) lput(":"); }
    lkv("rfe", ef_log[0xCA], 2); lkv("board", ef_log[0xC1], 2); lkv("xtal", ef_log[0xB9], 2); lkv("plan", ef_log[0xB8], 2);
    lkv("cc", (uint32_t)(ef_log[0xCB] << 8 | ef_log[0xCC]), 4); lkv("ther", (uint32_t)(ef_log[0xD0] << 8 | ef_log[0xD1]), 4);
    lkv("vid", (uint32_t)(ef_log[0x126] | ef_log[0x127] << 8), 4); lkv("did", (uint32_t)(ef_log[0x128] | ef_log[0x129] << 8), 4); lput("\r\n");
    lput("WIFI.efuse2");
    for (int i = 0; i < 16; i++) lkv("b", ef_log[0xB8 + i], 2);          /* 0xB8~0xC7: 채널플랜~옵션 */
    lput("\r\n");
    return !err;
}

/* 3d단계 - PHY/RF 파라미터 표 적용 (rtw8822c_phy_set_param 의 앞부분: BB/RF 전원, 3선식 설정, MAC/BB/AGC/RFK/RF-B/RF-A 표, 수정 발진기 용량)
 * 표 내용은 리눅스 rtw88 rtw8822c_table.c 에서 컷 D / RFE 1 / PCIe 조건으로 걸러 DRV\rtw8822c_phy.bin 로 만든 것(tools/genphy_rtw8822c.py).
 * 레코드: {u32 op<<28|주소, u32 값}  op 0=32비트쓰기 1=8비트쓰기 2=RF쓰기(경로는 구역이 정함) 3=기다림(마이크로초) */
static const uint8_t *g_phy; static uint32_t g_phyn; static uint8_t g_xtal;
static void hf_init(volatile uint8_t *m, int pre)
{
    R32(0x180C) |= 3; R32(0x180C) |= 0x10000000u; R32(0x410C) |= 3; R32(0x410C) |= 0x10000000u;
    if (pre) R32(0x1C3C) &= ~3u; else R32(0x1C3C) |= 3u;
}
static int rtw_phytbl(volatile uint8_t *m)
{
    const uint8_t *b = g_phy; uint32_t n = g_phyn;
    if (!b || n < 12 || b[0] != 'R' || b[1] != 'T' || b[2] != 'W' || b[3] != 'P') { lput("WIFI.phy res=skip why=no_file DRV\\rtw8822c_phy.bin\r\n"); return 0; }
    uint32_t ns = *(const uint32_t *)(b + 8);
    if (ns != 6 || n < 12 + 4 * ns) { lput("WIFI.phy res=fail why=bad_header\r\n"); return 0; }
    const uint32_t *cnt = (const uint32_t *)(b + 12);
    uint32_t tot = 0; for (uint32_t i = 0; i < ns; i++) tot += cnt[i];
    if (n != 12 + 4 * ns + 8 * tot) { lput("WIFI.phy res=fail why=bad_size\r\n"); return 0; }
    const uint32_t *rec = (const uint32_t *)(b + 12 + 4 * ns);
    R8(0x02) |= 3; R8(0x1F) |= 7; R32(0xEC) |= 0x07000000u;       /* BB/RF 전원 */
    R32(0xA70) = (R32(0xA70) & ~0x3FFu) | 0x3FFu;                 /* 낮은 속도 DPD 끄기 */
    hf_init(m, 1);
    uint32_t nw = 0, nr = 0;
    for (uint32_t sct = 0; sct < ns; sct++) {
        if (sct == 3) {                                           /* rtw_load_rfk_table 앞 준비 */
            R32(0x1E24) |= 1u << 17; R32(0x1CD0) |= (1u << 28) | (1u << 29) | (1u << 30); R32(0x1CD0) &= ~(1u << 31);
        }
        for (uint32_t i = 0; i < cnt[sct]; i++, rec++) {
            uint32_t a = rec[0], d = rec[1]; rec++;
            switch (a >> 28) {
            case 0: R32(a & 0xFFFF) = d; nw++; break;
            case 1: R8(a & 0xFFFF) = (uint8_t)d; nw++; break;
            case 2: {
                uint32_t ad = a & 0xFF; int path1 = (sct == 4);   /* 구역 4 = RF-B, 5 = RF-A */
                if (ad) { uint32_t p = (path1 ? 0x4C00u : 0x3C00u) + (ad << 2); R32(p) = (R32(p) & ~0xFFFFFu) | (d & 0xFFFFF); us(1); }
                else { R32(path1 ? 0x4108 : 0x1808) = d & 0x0FFFFFFFu; us(13); }
                nr++; break; }
            default: for (uint32_t t = 0; t < d / 1000; t++) us(1000); us((int)(d % 1000)); break;
            }
        }
    }
    uint32_t cap = g_xtal & 0x7F, v = cap | (cap << 7);
    R32(0x1040) = (R32(0x1040) & ~0xFFFC00u) | ((v << 10) & 0xFFFC00u);
    hf_init(m, 0);
    lput("WIFI.phy res=ok"); lkv("secs", ns, 1); lkv("recs", tot, 5); lkv("wr", nw, 5); lkv("rf", nr, 5); lkv("xtal", g_xtal, 2);
    lkv("r1040", R32(0x1040), 8); lkv("r1c3c", R32(0x1C3C), 8); lkv("r180c", R32(0x180C), 8); lkv("r3c60", R32(0x3C60), 8); lkv("r4c60", R32(0x4C60), 8); lkv("r1d90", R32(0x1D90), 8); lput("\r\n");
    return 1;
}

/* 3e단계 - 안테나 경로 설정 (rtw8822c_config_trx_mode, 2T2R=경로 A+B 모두 사용) + CCK 이득 경계 읽기
 * 리눅스 rtw88 rtw8822c.c: config_trx_mode / config_rx_path / config_tx_path / toggle_igi / bb_reset 를 그대로 옮김. 칩 종류는 0xF0 bit27 로 판정 */
static void wmk(volatile uint8_t *m, uint32_t o, uint32_t mask, uint32_t v)
{
    uint32_t sh = 0; while (!((mask >> sh) & 1)) sh++;
    R32(o) = (R32(o) & ~mask) | ((v << sh) & mask);
}
static void bbrst(volatile uint8_t *m) { R16(0x02) |= 1; R16(0x02) &= (uint16_t)~1; R16(0x02) |= 1; }
static int rtw_trxmode(volatile uint8_t *m)
{
    int ab = (R32(0xF0) >> 27) & 1; uint32_t path = ab ? 3 : 1;          /* BB_PATH_AB=3, A=1 */
    wmk(m, 0x1800, 0xFFFFF, 0x33312);                                  /* ORITXCODE (경로 A) */
    wmk(m, 0x4100, 0xFFFFF, ab ? 0x33312 : 0x11111);                   /* ORITXCODE2 (경로 B) */
    /* CCK 수신 경로 */
    if (path == 3) { wmk(m, 0x1A2C, 0x00600000, 1); wmk(m, 0x1A2C, 0x00060000, 1); wmk(m, 0x1A04, 0x0F000000, 1); }
    else { wmk(m, 0x1A2C, 0x00060000, 0); wmk(m, 0x1A2C, 0x00600000, 0); wmk(m, 0x1A04, 0x0F000000, 0); }
    /* OFDM 수신 경로 */
    uint32_t one = (path == 3);
    wmk(m, 0x1D30, 0x300, one); wmk(m, 0x1D30, 0x600000, one);
    wmk(m, 0xC44, 1u << 17, one); wmk(m, 0xC54, 1u << 20, one); wmk(m, 0xC38, 1u << 24, one);
    wmk(m, 0x824, 0x0F000000, path); wmk(m, 0x824, 0x000F0000, path);
    /* 송신 경로 (CCK 는 A 만, OFDM 1SS 는 A 만) */
    wmk(m, 0x1A04, 0xF0000000, 0x8); bbrst(m);
    if (path == 3) { wmk(m, 0x820, 0xFF, 0x31); wmk(m, 0x1E2C, 0xFFFF, 0x0400); }   /* tx_path=AB, 1SS 선택=A */
    else { wmk(m, 0x820, 0xFF, 0x11); wmk(m, 0x1E2C, 0xFF, 0x0); }
    bbrst(m); bbrst(m);
    /* toggle_igi */
    uint32_t igi = R32(0x1D70) & 0x7F;
    wmk(m, 0x1D70, 0x7F, igi - 2); wmk(m, 0x1D70, 0x7F00, igi - 2); wmk(m, 0x1D70, 0x7F, igi); wmk(m, 0x1D70, 0x7F00, igi);
    uint32_t ub = (((R32(0x1A98) >> 14) & 3) << 4) | ((R32(0x1AA8) >> 16) & 0xF);
    uint32_t lb = (((R32(0x1A98) >> 6) & 3) << 4) | ((R32(0x1A70) >> 24) & 0xF);
    lput("WIFI.trxm res=ok"); lkv("path", path, 1); lkv("igi", igi, 2); lkv("cckub", ub, 2); lkv("ccklb", lb, 2);
    lkv("r1800", R32(0x1800), 8); lkv("r4100", R32(0x4100), 8); lkv("r824", R32(0x824), 8); lkv("r820", R32(0x820), 8); lkv("r1e2c", R32(0x1E2C), 8);
    lkv("r1d30", R32(0x1D30), 8); lkv("r1a04", R32(0x1A04), 8); lkv("r1a2c", R32(0x1A2C), 8); lkv("r1d70", R32(0x1D70), 8); lput("\r\n");
    return 1;
}

/* 3f단계 - RF 초기화 (rtw8822c_rf_init: DAC 보정 → x2 확인 → 열 보정 → 전력 보정 → PA 바이어스)
 * 리눅스 rtw88 rtw8822c.c 함수들을 tools/genrfk_rtw8822c.py 로 uefi/rtwrfk.inc 에 줄 그대로 옮기고(BSD-3-Clause), rtwshim.h 의 껍데기로 칩에 연결한다.
 * 리눅스와 다른 점: 디버그 출력 생략, 오류는 개수만 센다. 보정 결과 저장/복원은 코드 그대로(처음 부팅이라 항상 새로 보정). */
#define shim_us us
#include "rtwshim.h"
const uint8_t *rtw_ef_phy_p;
#include "rtwrfk.inc"
static struct rtw_dev g_rd;
static int rtw_rfinit(volatile uint8_t *m)
{
    struct rtw_dev *d = &g_rd; rtw_ef_phy_p = ef_phy;
    d->m = m; d->hal.rf_path_num = ((R32(0xF0) >> 27) & 1) ? 2 : 1; d->nerr = 0; d->err1 = 0;
    rtw8822c_rf_dac_cal(d);
    lput("WIFI.rfk1 res=ok"); lkv("adckA", d->dm_info.dack_adck[0], 8); lkv("adckB", d->dm_info.dack_adck[1], 8);
    lkv("msbkA0", d->dm_info.dack_msbk[0][0][0], 4); lkv("msbkB1", d->dm_info.dack_msbk[1][1][0], 4);
    lkv("dckA", d->dm_info.dack_dck[0][0][0], 2); lkv("dckB", d->dm_info.dack_dck[1][0][0], 2); lkv("err", (uint32_t)d->nerr, 2); lput("\r\n");
    rtw8822c_rf_x2_check(d);
    rtw8822c_thermal_trim(d);
    rtw8822c_power_trim(d);
    rtw8822c_pa_bias(d);
    lput("WIFI.rfk2 res=ok");
    lkv("x2b8", rtw_read_rf(d, 0, 0xb8, RFREG_MASK), 5); lkv("th43A", rtw_read_rf(d, 0, 0x43, RFREG_MASK), 5); lkv("th43B", rtw_read_rf(d, 1, 0x43, RFREG_MASK), 5);
    lkv("pa60A", rtw_read_rf(d, 0, 0x60, RFREG_MASK), 5); lkv("pa60B", rtw_read_rf(d, 1, 0x60, RFREG_MASK), 5);
    lkv("dpd", R32(0xA70) & 0x3FF, 3); lkv("ppg", ef_phy[0x1ef], 2); lkv("err", (uint32_t)d->nerr, 2); lput("\r\n");
    return d->nerr == 0;
}

/* 3g단계 - 채널 설정 (rtw8822c_set_channel: BB → MAC → RF → IGI 토글). 2.4GHz 6번(20MHz) 후 5GHz 36번(20MHz) 순서로 두 번 설정해 보고 RF 0x18 읽기로 확인.
 * 마지막은 2.4GHz 6번으로 되돌려 둔다. 리눅스 rtw88 함수를 tools/genchan_rtw8822c.py 로 uefi/rtwchan.inc 에 그대로 옮겼다. */
#include "rtwchan.inc"
static int rtw_chan(volatile uint8_t *m)
{
    struct rtw_dev *d = &g_rd; d->m = m; d->nerr = 0;
    static const uint8_t chs[3] = { 6, 36, 6 };
    for (int i = 0; i < 3; i++) {
        rtw8822c_set_channel(d, chs[i], RTW_CHANNEL_WIDTH_20, 0);
        lput("WIFI.chan res=ok"); lkv("ch", chs[i], 2); lkv("rf18A", rtw_read_rf(d, 0, 0x18, RFREG_MASK), 5); lkv("rf18B", rtw_read_rf(d, 1, 0x18, RFREG_MASK), 5);
        lkv("r1d70", R32(0x1D70), 8); lkv("r1a00", R32(0x1A00), 8); lkv("r0c1c", R32(0xC1C), 8); lkv("r0c60", R32(0xC60), 8); lkv("r1ee8", R32(0x1EE8), 8); lput("\r\n");
    }
    return 1;
}

/* 4a단계 - 수신 링(RX ring) 설정 + 채널 돌며 주변 공유기(AP) 신호(beacon) 듣기
 * 4b단계 - 송신 링(MGMT 큐) 설정 + 송신 전력 기준값(efuse) 쓰기 + probe request 를 보내 probe response 가 오는지 확인 (인증 정보 없음, 공개 프레임만 송수신)
 * 리눅스 rtw88 pci.c: rtw_pci_reset_buf_desc / rtw_pci_rx_napi / rtw_pci_tx_write_data, tx.c: rtw_tx_fill_tx_desc / rtw_tx_mgmt_pkt_info_update 순서·필드를 따름.
 * 수신 서술자 24바이트, 수신 BD 8바이트{buf_size,total_pkt_size,dma}, 송신 BD 16바이트(둘로 나눈 8바이트: 서술자 48바이트 + 프레임), 송신 서술자 48바이트. */
#define RXN 128
#define RXBUF 4096
#define TXN 8
static uint32_t g_dmasz;

/* ====== 5.0.157: 실시간 실행 — 칩 초기화·스캔·접속·감시를 MSX 프레임 사이사이에 조금씩 (코루틴) ======
 * 이 PC(1번 V14-ARE)의 UEFI 에는 MP Services 가 없어 두 번째 코어를 쓸 수 없다. 그래서 기다림(pump)마다 메인 루프로 돌아가는
 * 협력형 코루틴으로 돌린다. 기다리는 시간은 g_clk(ms 시계)로 재므로 프레임 길이와 상관없다. 코루틴 밖(인라인)에서는 예전과 똑같이 막고 돈다. */
static uint32_t (*g_clk)(void);
static uint32_t g_inl_ms;
static int g_live, g_in_co, g_linked, g_ready, g_quiet;
static uint8_t g_lb[6]; static int g_lbon; static uint32_t g_lastbcn;
static uint32_t tms(void) { return g_live ? g_clk() : g_inl_ms; }
static void *g_co_sp, *g_main_sp;
static uint8_t g_costk[262144] __attribute__((aligned(16)));
void rtw_co_switch(void **save, void *to);
__asm__(
    ".text\n.p2align 4\n.globl rtw_co_switch\nrtw_co_switch:\n"
    "pushq %rbp\npushq %rbx\npushq %rdi\npushq %rsi\npushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
    "subq $168, %rsp\n"
    "movups %xmm6, 0(%rsp)\nmovups %xmm7, 16(%rsp)\nmovups %xmm8, 32(%rsp)\nmovups %xmm9, 48(%rsp)\nmovups %xmm10, 64(%rsp)\n"
    "movups %xmm11, 80(%rsp)\nmovups %xmm12, 96(%rsp)\nmovups %xmm13, 112(%rsp)\nmovups %xmm14, 128(%rsp)\nmovups %xmm15, 144(%rsp)\n"
    "movq %rsp, (%rcx)\nmovq %rdx, %rsp\n"
    "movups 0(%rsp), %xmm6\nmovups 16(%rsp), %xmm7\nmovups 32(%rsp), %xmm8\nmovups 48(%rsp), %xmm9\nmovups 64(%rsp), %xmm10\n"
    "movups 80(%rsp), %xmm11\nmovups 96(%rsp), %xmm12\nmovups 112(%rsp), %xmm13\nmovups 128(%rsp), %xmm14\nmovups 144(%rsp), %xmm15\n"
    "addq $168, %rsp\n"
    "popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %rsi\npopq %rdi\npopq %rbx\npopq %rbp\nret\n");
static void (*g_co_fn)(void); static volatile int g_co_done;
static void co_yield(void) { rtw_co_switch(&g_co_sp, g_main_sp); }
static void co_tramp(void) { g_co_fn(); g_co_done = 1; for (;;) co_yield(); }
static void co_init(void (*fn)(void))
{
    g_co_fn = fn; g_co_done = 0;
    uint64_t *a = (uint64_t *)((((uintptr_t)g_costk + sizeof g_costk) & ~(uintptr_t)15) - 64);   /* ret 자리 (16바이트 정렬) */
    a[0] = (uint64_t)(uintptr_t)co_tramp; a[1] = 0;
    uint8_t *sp = (uint8_t *)a - 232; for (int i = 0; i < 232; i++) sp[i] = 0;
    g_co_sp = sp;
}
static void co_resume(void) { if (g_co_done) return; g_in_co = 1; rtw_co_switch(&g_main_sp, g_co_sp); g_in_co = 0; }
typedef struct { uint8_t bssid[6], ch, sec, rate, ssidn; uint8_t ssid[33]; uint16_t cnt; uint8_t rsnlen, rsn[48]; uint8_t pw; } Bss;
static Bss bss[160]; static int nbss;
static uint8_t *g_bd, *g_bufs, *g_txbd[2], *g_txpk[2]; static uint32_t g_rp, g_txwp_q[2], g_prsp, g_prlog; static uint8_t g_mac[6];
/* 연결 과정에서 받을 프레임 보관함 (비콘·남의 응답은 넣지 않음) */
#define QN 40
typedef struct { uint16_t len; uint8_t fc0; uint8_t ch; uint32_t w3; uint8_t d[1700]; } QPkt;
static QPkt g_q[QN]; static int g_qn, g_listen, g_bcq; static uint32_t g_qdrop, g_qseen, g_qhist[8];
static int utf8ok(const uint8_t *s, int n)
{
    for (int i = 0; i < n; ) {
        uint8_t c = s[i];
        if (c < 0x20 || c == 0x7F) return 0;
        if (c < 0x80) { i++; continue; }
        int k = (c >= 0xF0 && c < 0xF5) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC2) ? 1 : -1;
        if (k < 0 || i + k >= n) return 0;
        for (int j = 1; j <= k; j++) if ((s[i + j] & 0xC0) != 0x80) return 0;
        i += k + 1;
    }
    return 1;
}
static void put_ssid(const uint8_t *s, int n)
{
    if (n == 0xFF) lput("(없음)"); else if (n == 0) lput("(숨김)");
    else if (utf8ok(s, n)) { char t[40]; int i; for (i = 0; i < n && i < 33; i++) t[i] = (char)s[i]; t[i] = 0; lput(t); }
    else for (int j = 0; j < n; j++) { char t[2] = { (s[j] >= 0x20 && s[j] < 0x7F) ? (char)s[j] : '?', 0 }; lput(t); }
}
/* 링에 쌓인 수신 프레임을 모두 읽고 비움. 반환: 읽은 개수. rx/bc/c2/bad 는 호출자가 모음 */
static uint32_t g_rxh[48], g_rxh_data[48], g_crcbad, g_crcbad_rate[48];   /* 5.2.99 진단: 받은 속도 분포(전체·데이터), 깨진 프레임(CRC) 수·속도 */
static uint32_t g_txr_n[3], g_txr_b8[3], g_txr_mx[3], g_bcnrate, g_bcn_n;   /* 5.2.97 진단: 속도별 송신 보고(건수·재시도 합·한계) , 비콘 수신 속도 */
static uint32_t g_c2hn;                                /* 5.2.94 진단: 펌웨어 보고 기록 개수 */
static void rx_drain(volatile uint8_t *m, uint32_t ch, uint32_t *rx, uint32_t *bc, uint32_t *c2, uint32_t *bad, uint32_t *hwo)
{
    uint32_t v = R32(0x3B4), hw = (v >> 16) & 0xFFF, cnt = (hw + RXN - g_rp) % RXN; *hwo = hw;
    for (uint32_t k = 0; k < cnt; k++) {
        uint8_t *b = g_bufs + g_rp * RXBUF; uint32_t w0 = *(volatile uint32_t *)b, w2 = *(volatile uint32_t *)(b + 8), w3 = *(volatile uint32_t *)(b + 12);
        uint32_t plen = w0 & 0x3FFF, drv = ((w0 >> 16) & 15) * 8, sh = (w0 >> 24) & 3, off = 24 + drv + sh;
        (*rx)++;
        if ((w2 >> 28) & 1) {
            (*c2)++;
            if (off + 16 <= RXBUF) { const uint8_t *c = b + off; int ix = c[7] == 0 ? 0 : c[7] == 3 ? 1 : c[7] == 4 ? 2 : -1; if (ix >= 0) { g_txr_n[ix]++; g_txr_b8[ix] += c[8]; if (c[8] >= 0x0F) g_txr_mx[ix]++; } }
            if (g_c2hn < 80 && off + 16 <= RXBUF) { g_c2hn++; const uint8_t *c = b + off; lput("WIFI.c2h len="); lhex(plen, 3); lput(" b="); for (int j = 0; j < 14; j++) { lhex(c[j], 2); lput(j < 13 ? " " : ""); } lput("\r\n"); }   /* 5.2.94 진단: 펌웨어 보고 원문 */
        }
        else if ((w0 & 0xC000) || plen < 28 || off + plen > RXBUF) { (*bad)++; g_crcbad++; g_crcbad_rate[(w3 & 0x7F) < 48 ? (w3 & 0x7F) : 47]++; }
        else {
            const uint8_t *f = b + off; int type = (f[0] >> 2) & 3, st = (f[0] >> 4) & 15;
            { uint32_t rr = (w3 & 0x7F) < 48 ? (w3 & 0x7F) : 47; g_rxh[rr]++; if (type == 2) g_rxh_data[rr]++; }
            if (g_listen && !(type == 0 && (st == 8 || st == 4 || st == 5)) && (f[4] & 1 ? g_bcq : (!__builtin_memcmp(f + 4, g_mac, 6))) && plen - 4 <= 1700) {
                g_qhist[g_qseen++ & 7] = (uint32_t)(f[0] << 8 | f[1]); if (g_qn < QN) { QPkt *qp = &g_q[g_qn++]; qp->len = (uint16_t)(plen - 4); qp->fc0 = f[0]; qp->ch = (uint8_t)ch; qp->w3 = w3; for (uint32_t j = 0; j < plen - 4; j++) qp->d[j] = f[j]; } else g_qdrop++;
            }
            if (type == 0 && (st == 8 || st == 5) && plen >= 40) {
                (*bc)++;
                if (g_lbon && st == 8 && !__builtin_memcmp(f + 16, g_lb, 6)) { g_lastbcn = tms(); g_bcnrate = w3 & 0x7F; g_bcn_n++; }   /* 연결된 AP 의 비콘 (끊김 감시) */
                int mine = 1; for (int j = 0; j < 6; j++) if (f[4 + j] != g_mac[j]) mine = 0;
                if (st == 5 && mine) {
                    g_prsp++;
                    if (g_prlog < 12) { g_prlog++; lput("WIFI.prsp ch="); lhex(ch, 2); lput(" from="); for (int j = 0; j < 6; j++) { lhex(f[10 + j], 2); if (j < 5) lput(":"); } lkv("rate", w3 & 0x7F, 2); lput(" ssid=");
                        const uint8_t *ie = f + 36, *end = f + plen - 4; while (ie + 2 <= end && ie + 2 + ie[1] <= end) { if (ie[0] == 0) { put_ssid(ie + 2, ie[1] > 32 ? 32 : ie[1]); break; } ie += 2 + ie[1]; } lput("\r\n"); }
                }
                int idx = -1; for (int i = 0; i < nbss; i++) { int same = 1; for (int j = 0; j < 6; j++) if (bss[i].bssid[j] != f[16 + j]) same = 0; if (same) { idx = i; break; } }
                if (idx < 0 && nbss < 160) { idx = nbss++; for (int j = 0; j < 6; j++) bss[idx].bssid[j] = f[16 + j]; bss[idx].ssidn = 0xFF; bss[idx].cnt = 0; bss[idx].ch = (uint8_t)ch; bss[idx].sec = 0; bss[idx].pw = 0; }
                if (idx >= 0) {
                    bss[idx].cnt++; bss[idx].rate = (uint8_t)(w3 & 0x7F);
                    if ((w3 & 0x7F) <= 3 && drv >= 8) { uint8_t pwd = b[25]; if (pwd > 0 && pwd < 100 && pwd > bss[idx].pw) bss[idx].pw = pwd; }   /* CCK 프레임의 PHY 상태 PWDB (신호 세기 추정) */
                    uint16_t cap = (uint16_t)(f[34] | f[35] << 8); if (cap & 0x10) bss[idx].sec |= 1;
                    const uint8_t *ie = f + 36, *end = f + plen - 4;
                    while (ie + 2 <= end && ie + 2 + ie[1] <= end) {
                        if (ie[0] == 0 && bss[idx].ssidn == 0xFF) { int n = ie[1] > 32 ? 32 : ie[1]; for (int j = 0; j < n; j++) bss[idx].ssid[j] = ie[2 + j]; bss[idx].ssid[n] = 0; bss[idx].ssidn = (uint8_t)n; }
                        else if (ie[0] == 3 && ie[1] == 1) bss[idx].ch = ie[2];
                        else if (ie[0] == 48) { bss[idx].sec |= 2; if (ie[1] <= 48) { bss[idx].rsnlen = ie[1]; for (int j = 0; j < ie[1]; j++) bss[idx].rsn[j] = ie[2 + j]; } }
                        else if (ie[0] == 221 && ie[1] >= 4 && ie[2] == 0 && ie[3] == 0x50 && ie[4] == 0xF2 && ie[5] == 1) bss[idx].sec |= 4;
                        ie += 2 + ie[1];
                    }
                }
            }
        }
        uint8_t *e = g_bd + g_rp * 8; *(uint16_t *)e = RXBUF; *(uint16_t *)(e + 2) = 0; *(uint32_t *)(e + 4) = (uint32_t)(uintptr_t)b;
        *(volatile uint32_t *)b = 0; *(volatile uint32_t *)(b + 8) = 0;
        if (++g_rp >= RXN) g_rp = 0;
    }
    R16(0x3B4) = (uint16_t)g_rp;
}
/* 송신 전력: 리눅스는 efuse 기준값 + 속도별 보정 + 규정 한도. 여기서는 efuse 의 그룹별 기준값에서 8(=2dB) 낮춘 값을 기준으로 쓰고 속도별 보정은 0.
 * 규정에 맞춘 정식 값이 아님(시험용, 2.4GHz 만 사용). 경로 A/B 모두 기록. */
static void rtw_txpwr2g(volatile uint8_t *m, int ch)
{
    int g = ch <= 2 ? 0 : ch <= 5 ? 1 : ch <= 8 ? 2 : ch <= 11 ? 3 : ch <= 13 ? 4 : 5;
    static const uint32_t refc[2] = { 0x18A0, 0x41A0 }, refo[2] = { 0x18E8, 0x41E8 };
    uint32_t pc[2], po[2];
    for (int p = 0; p < 2; p++) {
        uint32_t c = ef_log[0x10 + 42 * p + g], o = ef_log[0x10 + 42 * p + 6 + g];
        if (c == 0xFF || c > 0x7F) c = 0x28; if (o == 0xFF || o > 0x7F) o = 0x28;      /* 유효 범위는 0x7F 까지 (이전엔 0x3F 로 잘못 막아 efuse 값 0x4D·0x5C 를 버리고 0x28 로 낮게 썼음) */
        pc[p] = c > 4 ? c - 4 : 0; po[p] = o > 4 ? o - 4 : 0;
        wmk(m, 0x1C90, 1u << 15, 0); wmk(m, refc[p], 0x7F0000, pc[p]);
        wmk(m, 0x1C90, 1u << 15, 0); wmk(m, refo[p], 0x1FC00, po[p]);
    }
    for (uint32_t r = 0; r <= 0x3C; r += 4) { wmk(m, 0x1C90, 1u << 15, 0); R32(0x3A00 + r) = 0; }
    lput("WIFI.txpwr res=ok"); lkv("ch", (uint32_t)ch, 2); lkv("grp", (uint32_t)g, 1); lkv("cckA", pc[0], 2); lkv("cckB", pc[1], 2); lkv("ofdmA", po[0], 2); lkv("ofdmB", po[1], 2);
    lkv("efA", ef_log[0x10 + g], 2); lkv("efB", ef_log[0x10 + 42 + g], 2); lput("\r\n");
}
/* 송신이 공중에 안 나가는 원인 후보 = 블루투스 공존 허가(GNT_WL). rtw88 의 coex 초기화(rtw8822c_coex_cfg_init)와
 * DPK 의 GNT 고정 방법(0x70 bit26, 간접 0x38 바이트1=0x77)을 따라 무선 쪽 허가를 소프트웨어로 켠다 (블루투스는 쓰지 않는 시험 상태).
 * 간접 읽기 = 0x1700 에 0x800F0000|주소 쓰고 0x1708 읽기, 쓰기 = 0x1704 에 값, 0x1700 에 0xC00F0000|주소 */
static uint32_t coex_rd(volatile uint8_t *m, uint32_t a) { R32(0x1700) = 0x800F0000u | a; return R32(0x1708); }
static void coex_wr(volatile uint8_t *m, uint32_t a, uint32_t v) { R32(0x1704) = v; R32(0x1700) = 0xC00F0000u | a; }
static void rtw_coexfix(volatile uint8_t *m)
{
    uint32_t g0 = coex_rd(m, 0x38), r70 = R32(0x70), q0 = R8(0x4C6), p0 = R16(0x522);
    R32(0x40) |= (1u << 5) | (1u << 9);                 /* PTA 핀 켜기 */
    R8(0x4C6) = (uint8_t)((R8(0x4C6) | 0x10) & ~0x20);   /* PTA_WL_TX_EN 켜고 EDCCA 끔 */
    R16(0x762) |= 1u << 12;                              /* GNT_BT 극성 */
    R8(0x4FC) &= (uint8_t)~0x03;                         /* BTCCA 제어 끔 */
    R32(0x70) |= 1u << 26;                               /* GNT 소프트웨어 제어 */
    coex_wr(m, 0x38, (coex_rd(m, 0x38) & ~0xFF00u) | 0x7700u);   /* 바이트1 = 0x77 : 무선 허가 고정 */
    R16(0x522) = 0;                                      /* 송신 일시정지 해제 */
    lput("WIFI.coex res=ok"); lkv("g38_before", g0, 8); lkv("g38_after", coex_rd(m, 0x38), 8); lkv("r70_before", r70, 8); lkv("r70", R32(0x70), 8);
    lkv("q4c6_before", q0, 2); lkv("q4c6", R8(0x4C6), 2); lkv("txpause_before", p0, 4); lkv("txpause", R16(0x522), 4); lkv("r762", R16(0x762), 4); lkv("r40", R32(0x40), 8); lput("\r\n");
}
/* 큐 q(0=MGMT, 1=BE)로 프레임 한 개 보냄. 반환 1=칩이 읽어 감(하드웨어 인덱스가 따라옴).
 * 슬롯 768바이트. MGMT 는 칩이 순서번호를 붙이고(en_hwseq), BE(데이터)는 헤더의 순서번호를 그대로 씀 */
static uint16_t g_txsn;
static int tx_q(volatile uint8_t *m, int q, const uint8_t *fr, uint32_t len, uint32_t rate)
{
    static const uint32_t idxreg[2] = { 0x3B0, 0x3A8 };
    uint32_t wp = g_txwp_q[q]; uint8_t *pk = g_txpk[q] + wp * 768; uint32_t *d = (uint32_t *)pk;
    if (len > 700) return 0;
    for (int i = 0; i < 12; i++) d[i] = 0;
    uint32_t bmc = fr[4] & 1;
    d[0] = len | (48u << 16) | (bmc << 24) | (1u << 26) | (q == 0 ? (1u << 31) : 0);   /* 크기, 오프셋, bmc, ls, (MGMT) dis_qselseq */
    d[1] = 0 | ((q == 0 ? 18u : 0u) << 8) | (8u << 16);                                 /* macid 0, qsel(MGMT=18 / TID0), rate_id */
    d[3] = (1u << 8) | (1u << 10);                                                      /* use_rate, dis_rate_fallback */
    d[4] = rate & 0x7F;
    d[8] = q == 0 ? (1u << 15) : 0;                                                     /* MGMT: en_hwseq */
    if (q == 1) d[9] = (uint32_t)(((fr[22] | fr[23] << 8) >> 4) & 0xFFF) << 12;          /* 데이터는 SW_SEQ(W9 23:12)=헤더 순번. 0 으로 두면 칩이 모든 데이터 프레임을 순번 0 으로 보내 AP 가 재전송을 중복으로 버림 (rtw88 tx.c 와 같음) */
    if (q == 1) { g_txsn = (uint16_t)((g_txsn + 1) & 0xFFF); d[2] |= 1u << 19; d[6] = g_txsn; }   /* 5.2.94 진단: 송신 결과 보고(C2H) 요청 — AP 가 ACK 했는지 확인 */
    for (uint32_t i = 0; i < len; i++) pk[48 + i] = fr[i];
    uint8_t *e = g_txbd[q] + wp * 16; uint32_t pa = (uint32_t)(uintptr_t)pk, tot = 48 + len;
    *(uint16_t *)(e + 0) = 48; *(uint16_t *)(e + 2) = (uint16_t)((tot - 1) / 128 + 1); *(uint32_t *)(e + 4) = pa;
    *(uint16_t *)(e + 8) = (uint16_t)len; *(uint16_t *)(e + 10) = 0; *(uint32_t *)(e + 12) = pa + 48;
    uint32_t nwp = (wp + 1) % TXN;
    R16(idxreg[q]) = (uint16_t)nwp;
    int ok = 0;
    for (int t = 0; t < 20000; t++) { us(1); if ((((R32(idxreg[q])) >> 16) & 0xFFF) == nwp) { ok = 1; break; } }
    g_txwp_q[q] = nwp;
    return ok;
}
static int tx_mgmt(volatile uint8_t *m, const uint8_t *fr, uint32_t len, uint32_t rate) { return tx_q(m, 0, fr, len, rate); }
/* ===================== 5.0.150: 접속 (인증 → 결합 → WPA2 4-way → DHCP → 게이트웨이 ping) =====================
 * 모든 큰 버퍼는 static (두 번째 코어는 스택이 작을 수 있음). 비밀번호·PMK·키는 로그에 절대 쓰지 않음. */
#include "wpa.h"
static char g_ssid[33], g_pass[65];
void rtwdet_setcred(const char *s, const char *p)
{
    int i; for (i = 0; s && s[i] && i < 32; i++) g_ssid[i] = s[i]; g_ssid[i] = 0;
    for (i = 0; p && p[i] && i < 64; i++) g_pass[i] = p[i]; g_pass[i] = 0;
}
static uint8_t g_bssid[6], g_chan, g_txpn[6], g_myip[4], g_gwip[4], g_mask[4], g_dns[4], g_gwmac[6], g_pmk[32], g_ptk[48], g_gtk[32], g_rsnie[40], g_apcaps[2];
static int g_rsniel, g_have_tk, g_have_gtk; static uint16_t g_seq, g_aid;
static Aes128 g_aes_tk, g_aes_gtk;
static QPkt g_cur; static uint8_t g_fr[800], g_pl[1800], g_txp[700], g_ee[320];
static uint8_t g_dauth, g_grp3, g_dec, g_rej; static uint32_t g_dbg;
static void lip(const uint8_t *a) { for (int i = 0; i < 4; i++) { char t[4]; int v = a[i], n = 0; if (v >= 100) t[n++] = (char)('0' + v / 100); if (v >= 10) t[n++] = (char)('0' + (v / 10) % 10); t[n++] = (char)('0' + v % 10); t[n] = 0; lput(t); if (i < 3) lput("."); } }
static void lmac(const uint8_t *a) { for (int j = 0; j < 6; j++) { lhex(a[j], 2); if (j < 5) lput(":"); } }
/* ms 밀리초 기다리며 수신 링을 비움. 코루틴 안이면 기다리는 동안 메인 루프(MSX 한 프레임)로 돌려보냄. 인라인이면 예전처럼 막고 기다림 */
static void pumpx(volatile uint8_t *m, int ms, uint32_t ch, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *e, uint32_t *h)
{
    if (!g_in_co) { wait_ms(ms); g_inl_ms += (uint32_t)ms; rx_drain(m, ch, a, b, c, e, h); return; }
    uint32_t end = tms() + (uint32_t)ms;
    for (;;) { rx_drain(m, ch, a, b, c, e, h); if ((int32_t)(tms() - end) >= 0) break; co_yield(); }
}
static void pump(volatile uint8_t *m, int ms)
{
    uint32_t a = 0, b = 0, c = 0, e = 0, h = 0;
    if (!g_in_co) { for (int i = 0; i < ms; i++) { wait_ms(1); g_inl_ms++; rx_drain(m, g_chan, &a, &b, &c, &e, &h); } return; }
    pumpx(m, ms, g_chan, &a, &b, &c, &e, &h);
}
static int qpop(int data)
{
    for (int i = 0; i < g_qn; i++) {
        int ty = (g_q[i].fc0 >> 2) & 3;
        if ((data && ty == 2) || (!data && ty == 0)) { g_cur = g_q[i]; for (int j = i; j < g_qn - 1; j++) g_q[j] = g_q[j + 1]; g_qn--; return 1; }
    }
    return 0;
}
static void qlog(void)
{
    lkv("seen", g_qseen, 4); lkv("drop", g_qdrop, 3); lput(" last="); for (int i = 0; i < 8; i++) { lhex(g_qhist[(g_qseen + 8 - 1 - (uint32_t)i) & 7], 4); lput(" "); }
}
/* 관리프레임(AP→나) 기다림. 1=받음 0=시간초과 -1=연결해제 받음 */
static int wait_mgmt(volatile uint8_t *m, uint8_t fc0, int ms)
{
    for (uint32_t t0 = tms(); (int32_t)(tms() - t0) < ms; ) {
        pump(m, 1);
        while (qpop(0)) {
            if (g_cur.len < 26 || __builtin_memcmp(g_cur.d + 10, g_bssid, 6)) continue;
            if (g_cur.fc0 == fc0) return 1;
            if (g_cur.fc0 == 0xC0 || g_cur.fc0 == 0xA0) { g_dauth = g_cur.d[24]; return -1; }
        }
    }
    return 0;
}
/* 받은 데이터프레임(g_cur) → 평문(LLC 부터)을 g_pl 에. 반환 길이, 0=버림 */
static int rxdata(void)
{
    const uint8_t *f = g_cur.d; int hl = 24 + ((f[0] & 0x80) ? 2 : 0);
    g_rej = 0;
    if (g_cur.len < hl + 8) { g_rej = 1; return 0; }                        /* 버린 이유 (진단) 1 짧음 */
    if ((f[1] & 3) == 3) { g_rej = 2; return 0; }                           /* 2 주소 4개(WDS) */
    if (!(f[1] & 2) || (f[1] & 1)) { g_rej = 3; return 0; }                 /* 3 AP→단말(FromDS) 아님 */
    if ((f[0] >> 4) & 4) { g_rej = 4; return 0; }                           /* 4 데이터 없는 프레임(null) */
    if (__builtin_memcmp(f + 10, g_bssid, 6)) { g_rej = 5; return 0; }      /* 5 다른 BSSID */
    int blen = g_cur.len - hl;
    if (f[1] & 0x40) {
        int mc = f[4] & 1; if (!(mc ? g_have_gtk : g_have_tk)) { g_dec = 6; return 0; }
        if (mc && g_grp3 == 2) { int n = tkip_decrypt(g_gtk, f, hl, f + hl, blen, g_pl); g_dec = n >= 0 ? 4 : 5; return n > 0 ? n : 0; }
        uint8_t pn[6]; int n = ccmp_decrypt(mc ? &g_aes_gtk : &g_aes_tk, f, hl, f + hl, blen, g_pl, pn);
        g_dec = n >= 8 ? 2 : 3; return n >= 8 ? n : 0;
    }
    g_dec = 1;
    if (blen > 1800) return 0;
    for (int i = 0; i < blen; i++) g_pl[i] = f[hl + i];
    return blen;
}
static uint32_t g_txrate = 4, g_drate = 4;               /* 데이터 프레임 속도 (0 CCK1M, 3 CCK11M, 4 OFDM6M). g_drate = 응답이 확인된 속도 */
static int tx_data(volatile uint8_t *m, const uint8_t *da, const uint8_t *pl, int pln, int enc)
{
    uint8_t *h = g_fr; for (int i = 0; i < 24; i++) h[i] = 0;
    if (pln > 640) return 0;
    h[0] = 0x08; h[1] = (uint8_t)(0x01 | (enc ? 0x40 : 0));
    for (int j = 0; j < 6; j++) { h[4 + j] = g_bssid[j]; h[10 + j] = g_mac[j]; h[16 + j] = da[j]; }
    uint16_t sq = (uint16_t)((g_seq++ & 0xFFF) << 4); h[22] = (uint8_t)sq; h[23] = (uint8_t)(sq >> 8);
    int len;
    if (enc) { for (int i = 0; i < 6; i++) if (++g_txpn[i]) break; len = 24 + ccmp_encrypt(&g_aes_tk, h, 24, g_txpn, 0, pl, pln, h + 24); }
    else { for (int i = 0; i < pln; i++) h[24 + i] = pl[i]; len = 24 + pln; }
    return tx_q(m, 1, g_fr, (uint32_t)len, g_txrate);
}
static void rnd32(uint8_t o[32])
{
    static uint32_t ctr; uint8_t b[64]; uint64_t t = __builtin_ia32_rdtsc();
    for (int i = 0; i < 64; i++) b[i] = 0;
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)(t >> (8 * i));
    for (int i = 0; i < 6; i++) { b[8 + i] = g_mac[i]; b[14 + i] = g_bssid[i]; }
    ctr++; b[20] = (uint8_t)ctr; b[21] = (uint8_t)(ctr >> 8); b[22] = (uint8_t)g_qseen; b[23] = (uint8_t)g_prsp;
    uint8_t h[20]; wpa_sha1(b, 64, h); for (int i = 0; i < 20; i++) o[i] = h[i];
    b[24]++; wpa_sha1(b, 64, h); for (int i = 0; i < 12; i++) o[20 + i] = h[i];
}
static const uint8_t k_llc_eap[8] = { 0xAA, 0xAA, 0x03, 0, 0, 0, 0x88, 0x8E };
static int g_eapol_enc;                 /* 1 = 그룹 키 답(G2) — AP 가 짝 키를 깐 뒤라 CCMP 로 감싸서 보냄 */
static uint8_t g_rc[8];                  /* 마지막으로 받아들인 EAPOL 재생 카운터 (더 큰 것만 받음, IEEE 802.11 12.7.2) */
static void eapol_send(volatile uint8_t *m, uint16_t info, int klen, const uint8_t *replay, const uint8_t *nonce, const uint8_t *kd, int kdl)
{
    uint8_t *p = g_txp; for (int i = 0; i < 300; i++) p[i] = 0;
    for (int i = 0; i < 8; i++) p[i] = k_llc_eap[i];
    uint8_t *e = p + 8; int bl = 95 + kdl;
    e[0] = 1; e[1] = 3;   /* EAPOL 판 1 — wpa_supplicant 기본값과 같음 (판 2 를 버리는 AP 가 있음, wpa_supplicant.conf eapol_version 설명) */ e[2] = (uint8_t)(bl >> 8); e[3] = (uint8_t)bl; e[4] = 2; e[5] = (uint8_t)(info >> 8); e[6] = (uint8_t)info; e[8] = (uint8_t)klen;
    for (int i = 0; i < 8; i++) e[9 + i] = replay[i];
    if (nonce) for (int i = 0; i < 32; i++) e[17 + i] = nonce[i];
    e[97] = (uint8_t)(kdl >> 8); e[98] = (uint8_t)kdl; for (int i = 0; i < kdl; i++) e[99 + i] = kd[i];
    uint8_t mic[16]; wpa_mic(g_ptk, e, 4 + bl, mic); for (int i = 0; i < 16; i++) e[81 + i] = mic[i];
    tx_data(m, g_bssid, p, 8 + 4 + bl, g_eapol_enc);
}
/* 4-way handshake. 반환 1=성공 */
static int do_4way(volatile uint8_t *m)
{
    uint8_t sn[32], an[32]; int havesn = 0, m1 = 0, m3 = 0, ok = 0;
    uint32_t t0_4w = tms();
    for (uint32_t t; (t = tms() - t0_4w) < 5000 && !ok; ) {
        pump(m, 1);
        while (!ok && qpop(1)) {
            int n = rxdata(); if (n < 8 + 99 || __builtin_memcmp(g_pl, k_llc_eap, 8)) continue;
            const uint8_t *e = g_pl + 8; int tot = 4 + ((e[2] << 8) | e[3]); if (e[1] != 3 || (e[4] != 2 && e[4] != 254) || tot > n - 8) continue;
            uint16_t info = (uint16_t)((e[5] << 8) | e[6]);
            if ((info & 0x88) == 0x88 && !(info & 0x100)) {                        /* 메시지 1 */
                for (int i = 0; i < 32; i++) an[i] = e[17 + i];
                if (!havesn) { rnd32(sn); havesn = 1; }
                wpa_ptk(g_pmk, g_bssid, g_mac, an, sn, g_ptk);
                eapol_send(m, 0x010A, 0, e + 9, sn, g_rsnie, g_rsniel);   /* RSN 은 M2·M4 의 키 길이 0 (wpa_supplicant 와 같음) */
                if (m1++ < 3) { lput("WIFI.eapol m1=ok m2=sent"); lkv("info", info, 4); lkv("apver", e[0], 1); lkv("rc", e[16], 2); lkv("t_ms", t, 4); lput("\r\n"); }
            } else if (havesn && (info & 0x1C0) == 0x1C0 + 0 && (info & 0x40) && (info & 0x100) && (info & 0x80)) {   /* 메시지 3 */
                for (int i = 0; i < 32; i++) if (an[i] != e[17 + i]) { lput("WIFI.eapol m3=fail why=anonce_changed\r\n"); return 0; }
                for (int i = 0; i < tot; i++) g_ee[i] = e[i];
                uint8_t mic[16]; for (int i = 0; i < 16; i++) g_ee[81 + i] = 0;
                wpa_mic(g_ptk, g_ee, tot, mic); int bad = 0; for (int i = 0; i < 16; i++) bad |= mic[i] ^ e[81 + i];
                if (bad) { lput("WIFI.eapol m3=fail why=mic_bad\r\n"); continue; }       /* 비밀번호가 틀리면 여기서 걸림 */
                int dl = (e[97] << 8) | e[98]; int gl = 0, gid = 0, haskd = 0;
                if ((info & 0x1000) && dl >= 16 && dl <= 200 && 99 + dl <= tot) {
                    int u = wpa_unwrap(g_ptk + 16, e + 99, dl, g_fr);
                    if (u > 0) { haskd = 1; lput("WIFI.kd elems="); for (int i = 0; i + 2 <= u; ) { int id = g_fr[i], l = g_fr[i + 1]; lhex((uint32_t)id, 2); lput("/"); lhex((uint32_t)l, 2); lput(" "); if (id == 0xDD && l == 0) break; if (i + 2 + l > u) break;
                        if (id == 0xDD && l >= 22 && g_fr[i + 2] == 0 && g_fr[i + 3] == 0x0F && g_fr[i + 4] == 0xAC && g_fr[i + 5] == 1) { gid = g_fr[i + 6] & 3; gl = l - 6; for (int j = 0; j < gl && j < 32; j++) g_gtk[j] = g_fr[i + 8 + j]; }
                        i += 2 + l; } lput("\r\n"); }
                }
                for (int i = 0; i < 8; i++) g_rc[i] = e[9 + i];
                g_eapol_enc = 0; eapol_send(m, 0x030A, 0, e + 9, 0, 0, 0);
                aes_key(&g_aes_tk, g_ptk + 32); g_have_tk = 1; for (int i = 0; i < 6; i++) g_txpn[i] = 0;
                if (gl == 16 && g_grp3 == 4) { aes_key(&g_aes_gtk, g_gtk); g_have_gtk = 1; g_bcq = 1; }
                else if (gl >= 16 && g_grp3 == 2) { g_have_gtk = 1; g_bcq = 1; }
                lput("WIFI.eapol m3=ok m4=sent"); lkv("kd", (uint32_t)haskd, 1); lkv("gtk_len", (uint32_t)gl, 2); lkv("gtk_id", (uint32_t)gid, 1); lkv("t_ms", t, 4); lput("\r\n");
                m3 = 1; ok = 1;
            }
        }
    }
    if (!ok) { lput("WIFI.eapol res=fail"); lkv("m1", (uint32_t)m1, 2); lkv("m3", (uint32_t)m3, 1); lput(" "); qlog(); lput("\r\n"); }
    return ok;
}
/* 연결된 뒤에 받은 EAPOL (IEEE 802.11-2020 12.7.6·12.7.7, wpa_supplicant 와 같은 처리).
 *  - M3 다시 옴 (AP 가 M4 를 못 받음): MIC 확인 → M4 다시 보냄. 키는 다시 설치하지 않음(PN 유지 — KRACK 방지).
 *  - 그룹 키 메시지 1 (G1, GTK 교체): 키 데이터 풀기 → 새 GTK 설치 → G2 보냄.
 *  - M1 (짝 키 다시 만들기): 기록만 하고 다시 접속 (드물다). */
static void rtw_lost(const char *why);
static void live_eapol(volatile uint8_t *m, int n)
{
    if (n < 8 + 99) return;
    const uint8_t *e = g_pl + 8; int tot = 4 + ((e[2] << 8) | e[3]);
    if (e[1] != 3 || (e[4] != 2 && e[4] != 254) || tot > n - 8 || tot > (int)sizeof g_ee) { lput("WIFI.eapol live res=skip why=bad_frame\r\n"); return; }
    uint16_t info = (uint16_t)((e[5] << 8) | e[6]);
    int newer = 0; for (int i = 0; i < 8; i++) if (e[9 + i] != g_rc[i]) { newer = e[9 + i] > g_rc[i]; break; }
    const char *kind = (info & 0x08) ? ((info & 0x100) ? "m3" : "m1") : "g1";
    lput("WIFI.eapol live kind="); lput(kind); lkv("info", info, 4); lkv("rc", e[16], 2); lkv("newer", (uint32_t)newer, 1);
    if (!newer) { lput(" res=skip why=old_replay\r\n"); return; }
    if (!(info & 0x100) || !(info & 0x80)) { lput(" res=skip why=no_mic_or_ack\r\n"); if ((info & 0x08) && (info & 0x80)) rtw_lost("ptk_rekey_m1"); return; }
    for (int i = 0; i < tot; i++) g_ee[i] = e[i];
    for (int i = 0; i < 16; i++) g_ee[81 + i] = 0;
    uint8_t mic[16]; wpa_mic(g_ptk, g_ee, tot, mic); int bad = 0; for (int i = 0; i < 16; i++) bad |= mic[i] ^ e[81 + i];
    if (bad) { lput(" res=fail why=mic_bad\r\n"); return; }
    for (int i = 0; i < 8; i++) g_rc[i] = e[9 + i];
    if (info & 0x08) {                                                     /* M3 다시 옴 → M4 */
        g_eapol_enc = 0; eapol_send(m, 0x030A, 0, e + 9, 0, 0, 0);       /* AP 가 M3 를 다시 보낸 것은 아직 키를 안 깔았다는 뜻 → 첫 M4 처럼 평문 */
        lput(" res=ok sent=m4\r\n"); return;
    }
    int dl = (e[97] << 8) | e[98], gl = 0, gid = 0;                        /* G1 → 새 GTK */
    if ((info & 0x1000) && dl >= 16 && dl <= 200 && 99 + dl <= tot) {
        int u = wpa_unwrap(g_ptk + 16, e + 99, dl, g_fr);
        for (int i = 0; u > 0 && i + 2 <= u; ) { int id = g_fr[i], l = g_fr[i + 1]; if (id == 0xDD && l == 0) break; if (i + 2 + l > u) break;
            if (id == 0xDD && l >= 22 && g_fr[i + 2] == 0 && g_fr[i + 3] == 0x0F && g_fr[i + 4] == 0xAC && g_fr[i + 5] == 1) { gid = g_fr[i + 6] & 3; gl = l - 6; for (int j = 0; j < gl && j < 32; j++) g_gtk[j] = g_fr[i + 8 + j]; }
            i += 2 + l; }
    }
    if (gl < 16) { lput(" res=fail why=no_gtk"); lkv("kd", (uint32_t)dl, 3); lput("\r\n"); return; }
    if (g_grp3 == 4) aes_key(&g_aes_gtk, g_gtk);
    g_have_gtk = 1;
    g_eapol_enc = 1; eapol_send(m, 0x0302, 0, e + 9, 0, 0, 0); g_eapol_enc = 0;
    lput(" res=ok sent=g2"); lkv("gtk_id", (uint32_t)gid, 1); lkv("gtk_len", (uint32_t)gl, 2); lput("\r\n");
}
static uint16_t csum(const uint8_t *p, int n) { uint32_t s = 0; for (int i = 0; i + 1 < n; i += 2) s += (uint32_t)(p[i] << 8 | p[i + 1]); if (n & 1) s += (uint32_t)(p[n - 1] << 8); while (s >> 16) s = (s & 0xFFFF) + (s >> 16); return (uint16_t)~s; }
static int ip_hdr(uint8_t *ip, int total, int proto, const uint8_t *src, const uint8_t *dst)
{
    for (int i = 0; i < 20; i++) ip[i] = 0;
    ip[0] = 0x45; ip[2] = (uint8_t)(total >> 8); ip[3] = (uint8_t)total; ip[5] = 1; ip[8] = 64; ip[9] = (uint8_t)proto;
    for (int i = 0; i < 4; i++) { ip[12 + i] = src[i]; ip[16 + i] = dst[i]; }
    uint16_t c = csum(ip, 20); ip[10] = (uint8_t)(c >> 8); ip[11] = (uint8_t)c; return 20;
}
static const uint8_t k_bc[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }, k_ip0[4] = { 0, 0, 0, 0 }, k_ipbc[4] = { 255, 255, 255, 255 };
static int dhcp_send(volatile uint8_t *m, int type, uint32_t xid, const uint8_t *reqip, const uint8_t *srv, int variant)
{
    uint8_t *p = g_txp; for (int i = 0; i < 400; i++) p[i] = 0;
    p[0] = 0xAA; p[1] = 0xAA; p[2] = 3; p[6] = 8; p[7] = 0;
    uint8_t *ip = p + 8, *udp = ip + 20, *b = udp + 8;
    b[0] = 1; b[1] = 1; b[2] = 6; if (variant == 1) b[10] = 0x80; b[4] = (uint8_t)(xid >> 24); b[5] = (uint8_t)(xid >> 16); b[6] = (uint8_t)(xid >> 8); b[7] = (uint8_t)xid;
    for (int i = 0; i < 6; i++) b[28 + i] = g_mac[i];
    uint8_t *o = b + 236; o[0] = 0x63; o[1] = 0x82; o[2] = 0x53; o[3] = 0x63; int n = 4;
    o[n++] = 53; o[n++] = 1; o[n++] = (uint8_t)type;
    if (variant != 2) { o[n++] = 61; o[n++] = 7; o[n++] = 1; for (int i = 0; i < 6; i++) o[n++] = g_mac[i]; }
    if (type == 3) { o[n++] = 50; o[n++] = 4; for (int i = 0; i < 4; i++) o[n++] = reqip[i]; o[n++] = 54; o[n++] = 4; for (int i = 0; i < 4; i++) o[n++] = srv[i]; }
    o[n++] = 55; o[n++] = 4; o[n++] = 1; o[n++] = 3; o[n++] = 6; o[n++] = 51; o[n++] = 255;
    int ul = 8 + 236 + n; udp[0] = 0; udp[1] = 68; udp[2] = 0; udp[3] = 67; udp[4] = (uint8_t)(ul >> 8); udp[5] = (uint8_t)ul;
    ip_hdr(ip, 20 + ul, 17, k_ip0, k_ipbc);
    return tx_data(m, k_bc, p, 8 + 20 + ul, g_have_tk);
}
/* want=2 OFFER, 5 ACK. 반환 1=받음 (g_myip 등 채움) */
static uint8_t g_srv[4];
static int dhcp_wait(volatile uint8_t *m, uint32_t xid, int want, int ms)
{
    for (int t = 0; t < ms; t++) {
        pump(m, 1);
        while (qpop(1)) {
            int n = rxdata();
            { int isd = n > 40 && g_pl[6] == 8 && g_pl[7] == 0 && g_pl[17] == 17 && g_pl[8 + 23] == 68 && g_pl[8 + 22] == 0;
              if (isd || g_dbg < 60) { if (!isd) g_dbg++; lput("WIFI.rxd fc="); lhex((uint32_t)(g_cur.d[0] << 8 | g_cur.d[1]), 4); lput(" a1="); lmac(g_cur.d + 4); lkv("len", g_cur.len, 4); lkv("f1", g_cur.d[1], 2); lkv("dec", g_dec, 1); lkv("n", (uint32_t)n, 4);
                if (n >= 8) { lput(" llc="); lhex((uint32_t)(g_pl[6] << 8 | g_pl[7]), 4); if (isd) { const uint8_t *bp = g_pl + 8 + 28; const uint8_t *oo = bp + 240; lkv("xid", (uint32_t)(bp[4] << 24 | bp[5] << 16 | bp[6] << 8 | bp[7]), 8); lkv("op", bp[0], 2); for (int q = 0; q + 3 < 40; ) { if (oo[q] == 53) { lkv("type", oo[q + 2], 2); break; } if (oo[q] == 255) break; q += 2 + oo[q + 1]; } } } lput("\r\n"); } }
            if (n < 8 + 20 + 8 + 240 || g_pl[6] != 8 || g_pl[7] != 0) continue;
            const uint8_t *ip = g_pl + 8; int ihl = (ip[0] & 15) * 4; if (ip[9] != 17 || ihl < 20 || 8 + ihl + 8 + 240 > n) continue;
            const uint8_t *u = ip + ihl, *b = u + 8; if (u[2] != 0 || u[3] != 68 || b[0] != 2) continue;
            if ((uint32_t)(b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7]) != xid || __builtin_memcmp(b + 28, g_mac, 6)) continue;
            const uint8_t *o = b + 236; int typ = 0; uint8_t mask[4] = {0}, gw[4] = {0}, dns[4] = {0}, sv[4] = {0}; uint32_t lease = 0;
            int len = n - 8 - ihl - 8 - 236;
            if (o[0] != 0x63 || o[1] != 0x82) continue;
            for (int i = 4; i + 2 <= len; ) {
                int c = o[i]; if (c == 255) break; if (c == 0) { i++; continue; } int l = o[i + 1]; if (i + 2 + l > len) break;
                if (c == 53) typ = o[i + 2]; else if (c == 1 && l == 4) for (int j = 0; j < 4; j++) mask[j] = o[i + 2 + j];
                else if (c == 3 && l >= 4) for (int j = 0; j < 4; j++) gw[j] = o[i + 2 + j];
                else if (c == 6 && l >= 4) for (int j = 0; j < 4; j++) dns[j] = o[i + 2 + j];
                else if (c == 54 && l == 4) for (int j = 0; j < 4; j++) sv[j] = o[i + 2 + j];
                else if (c == 51 && l == 4) lease = (uint32_t)(o[i + 2] << 24 | o[i + 3] << 16 | o[i + 4] << 8 | o[i + 5]);
                i += 2 + l;
            }
            if (typ != want) continue;
            for (int j = 0; j < 4; j++) { g_myip[j] = b[16 + j]; g_mask[j] = mask[j]; g_gwip[j] = gw[j]; g_dns[j] = dns[j]; g_srv[j] = sv[j]; }
            lput(want == 2 ? "WIFI.dhcp offer ip=" : "WIFI.dhcp ack ip="); lip(g_myip); lput(" mask="); lip(mask); lput(" gw="); lip(gw); lput(" dns="); lip(dns); lput(" srv="); lip(sv); lkv("lease", lease, 8); lkv("t_ms", (uint32_t)t, 4); lput("\r\n");
            return 1;
        }
    }
    return 0;
}
static void arp_service(volatile uint8_t *m)    /* 받은 ARP 요청이 내 주소를 찾으면 답함 */
{
    if (g_pl[6] != 8 || g_pl[7] != 6) return;
    const uint8_t *a = g_pl + 8;
    if (a[6] != 0 || a[7] != 1 || __builtin_memcmp(a + 24, g_myip, 4)) return;
    uint8_t *p = g_txp; for (int i = 0; i < 40; i++) p[i] = 0;
    p[0] = 0xAA; p[1] = 0xAA; p[2] = 3; p[6] = 8; p[7] = 6; uint8_t *r = p + 8;
    r[1] = 1; r[2] = 8; r[4] = 6; r[5] = 4; r[7] = 2;
    for (int i = 0; i < 6; i++) { r[8 + i] = g_mac[i]; r[18 + i] = a[8 + i]; }
    for (int i = 0; i < 4; i++) { r[14 + i] = g_myip[i]; r[24 + i] = a[14 + i]; }
    tx_data(m, a + 8, p, 8 + 28, g_have_tk);
}
static int arp_gw(volatile uint8_t *m, int probe)
{
    for (int rep = 0; rep < 4; rep++) {
        uint8_t *p = g_txp; for (int i = 0; i < 40; i++) p[i] = 0;
        p[0] = 0xAA; p[1] = 0xAA; p[2] = 3; p[6] = 8; p[7] = 6; uint8_t *r = p + 8;
        r[1] = 1; r[2] = 8; r[4] = 6; r[5] = 4; r[7] = 1;
        for (int i = 0; i < 6; i++) r[8 + i] = g_mac[i];
        for (int i = 0; i < 4; i++) { r[14 + i] = probe ? 0 : g_myip[i]; r[24 + i] = g_gwip[i]; }
        tx_data(m, k_bc, p, 8 + 28, g_have_tk);
        for (int t = 0; t < 600; t++) {
            pump(m, 1);
            while (qpop(1)) {
                int n = rxdata(); if (n < 8 + 28 || g_pl[6] != 8 || g_pl[7] != 6) continue;
                const uint8_t *a = g_pl + 8;
                if (a[7] == 2 && !__builtin_memcmp(a + 14, g_gwip, 4)) { for (int j = 0; j < 6; j++) g_gwmac[j] = a[8 + j]; lput(probe ? "WIFI.arp probe gw=" : "WIFI.arp gw="); lip(g_gwip); lput(" mac="); lmac(g_gwmac); lkv("t_ms", (uint32_t)(rep * 600 + t), 4); lput("\r\n"); return 1; }
                arp_service(m);
            }
        }
    }
    return 0;
}
static int ping_gw(volatile uint8_t *m)
{
    for (int rep = 0; rep < 4; rep++) {
        uint8_t *p = g_txp; for (int i = 0; i < 80; i++) p[i] = 0;
        p[0] = 0xAA; p[1] = 0xAA; p[2] = 3; p[6] = 8; p[7] = 0; uint8_t *ip = p + 8, *ic = ip + 20;
        ic[0] = 8; ic[4] = 0x12; ic[5] = 0x34; ic[7] = (uint8_t)(rep + 1); for (int i = 0; i < 16; i++) ic[8 + i] = (uint8_t)('a' + i);
        uint16_t c = csum(ic, 24); ic[2] = (uint8_t)(c >> 8); ic[3] = (uint8_t)c;
        ip_hdr(ip, 20 + 24, 1, g_myip, g_gwip);
        tx_data(m, g_gwmac, p, 8 + 20 + 24, g_have_tk);
        for (int t = 0; t < 800; t++) {
            pump(m, 1);
            while (qpop(1)) {
                int n = rxdata(); if (n < 8 + 28) continue;
                if (g_pl[6] == 8 && g_pl[7] == 6) { arp_service(m); continue; }
                const uint8_t *r = g_pl + 8; if (g_pl[6] != 8 || g_pl[7] != 0 || r[9] != 1) continue;
                const uint8_t *q = r + (r[0] & 15) * 4; if (q[0] == 0 && q[4] == 0x12 && q[5] == 0x34) { lput("WIFI.ping res=ok gw="); lip(g_gwip); lkv("seq", q[7], 2); lkv("t_ms", (uint32_t)(rep * 800 + t), 4); lput("\r\n"); return 1; }
            }
        }
    }
    return 0;
}
static int sig_of(const Bss *b);
static int rtw_connect(volatile uint8_t *m, struct rtw_dev *d)
{
    if (!g_ssid[0] || !g_pass[0]) { lput("WIFI.conn res=skip why=no_SSID_PASS_in_RTW.INI\r\n"); return 0; }
    if (!(g_pass[0] && g_pass[7])) { lput("WIFI.conn res=skip why=pass_too_short\r\n"); return 0; }
    int sl = 0; while (g_ssid[sl]) sl++;
    int bi = -1, bsg = -1;                                                  /* 같은 이름의 공유기가 여럿이면 신호가 가장 센 것 (윈도·리눅스와 같음) */
    for (int i = 0; i < nbss; i++) if (bss[i].ssidn == sl && !__builtin_memcmp(bss[i].ssid, g_ssid, (unsigned)sl) && (bss[i].sec & 2) && bss[i].ch < 14) { int sg = sig_of(&bss[i]); if (sg > bsg) { bsg = sg; bi = i; } }
    lput("WIFI.conn ssid="); put_ssid((const uint8_t *)g_ssid, sl); lkv("found", bi >= 0, 1); lkv("sig", (uint32_t)(bsg < 0 ? 0 : bsg), 2);
    if (bi < 0) { lput(" res=skip why=ssid_not_seen_on_2.4G_with_RSN\r\n"); return 0; }
    Bss *b = &bss[bi]; for (int j = 0; j < 6; j++) g_bssid[j] = b->bssid[j]; g_chan = b->ch;
    lput(" bssid="); lmac(g_bssid); lkv("ch", g_chan, 2); lput("\r\n");
    /* AP 의 RSN 정보 확인: 그룹/개별 암호, 인증방식 */
    const uint8_t *r = b->rsn; int rl = b->rsnlen, okp = 0, oka = 0, pc = 0, ac = 0; uint8_t grp[4] = { 0, 0, 0, 0 };
    if (rl >= 8) {
        for (int i = 0; i < 4; i++) grp[i] = r[2 + i];
        pc = r[6] | r[7] << 8; int o = 8;
        for (int i = 0; i < pc && o + 4 <= rl; i++, o += 4) if (r[o] == 0 && r[o + 1] == 0x0F && r[o + 2] == 0xAC && r[o + 3] == 4) okp = 1;
        if (o + 2 <= rl) { ac = r[o] | r[o + 1] << 8; o += 2; for (int i = 0; i < ac && o + 4 <= rl; i++, o += 4) if (r[o] == 0 && r[o + 1] == 0x0F && r[o + 2] == 0xAC && r[o + 3] == 2) oka = 1; }
        g_apcaps[0] = g_apcaps[1] = 0; if (o + 2 <= rl) { g_apcaps[0] = r[o]; g_apcaps[1] = r[o + 1]; }
    }
    g_grp3 = grp[3];
    lput("WIFI.rsn"); lkv("group", (uint32_t)(grp[3]), 2); lkv("pw", (uint32_t)pc, 2); lkv("ccmp", (uint32_t)okp, 1); lkv("akm", (uint32_t)ac, 2); lkv("psk", (uint32_t)oka, 1); lkv("caps", (uint32_t)(g_apcaps[0] | g_apcaps[1] << 8), 4); lput("\r\n");
    if (!okp || !oka) { lput("WIFI.conn res=skip why=AP_needs_other_cipher_or_akm\r\n"); return 0; }
    int ie = 0; g_rsnie[ie++] = 48; g_rsnie[ie++] = 20; g_rsnie[ie++] = 1; g_rsnie[ie++] = 0; for (int i = 0; i < 4; i++) g_rsnie[ie++] = grp[i];
    g_rsnie[ie++] = 1; g_rsnie[ie++] = 0; g_rsnie[ie++] = 0; g_rsnie[ie++] = 0x0F; g_rsnie[ie++] = 0xAC; g_rsnie[ie++] = 4;
    g_rsnie[ie++] = 1; g_rsnie[ie++] = 0; g_rsnie[ie++] = 0; g_rsnie[ie++] = 0x0F; g_rsnie[ie++] = 0xAC; g_rsnie[ie++] = 2; g_rsnie[ie++] = 0; g_rsnie[ie++] = 0; g_rsniel = ie;
    uint64_t t0 = __builtin_ia32_rdtsc(); wpa_pmk(g_pass, (const uint8_t *)g_ssid, sl, g_pmk); uint64_t t1 = __builtin_ia32_rdtsc();
    lput("WIFI.pmk res=done"); lkv("kcycles", (uint32_t)((t1 - t0) >> 10), 8); lput("\r\n");
    /* 채널 맞추고 수신 보관 켜기 */
    rtw8822c_set_channel(d, g_chan, RTW_CHANNEL_WIDTH_20, 0); rtw_txpwr2g(m, g_chan);
    g_listen = 1; g_bcq = 0; g_qn = 0; g_qdrop = 0; g_have_tk = g_have_gtk = 0; pump(m, 30);
    uint8_t fr[160];
    int auth = 0;
    for (int tr = 0; tr < 4 && !auth; tr++) {
        for (int i = 0; i < 30; i++) fr[i] = 0;
        fr[0] = 0xB0; for (int j = 0; j < 6; j++) { fr[4 + j] = g_bssid[j]; fr[10 + j] = g_mac[j]; fr[16 + j] = g_bssid[j]; }
        fr[24] = 0; fr[25] = 0; fr[26] = 1; fr[27] = 0; fr[28] = 0; fr[29] = 0;
        int tk = tx_mgmt(m, fr, 30, 0);
        int w = wait_mgmt(m, 0xB0, 600);
        lput("WIFI.auth"); lkv("try", (uint32_t)tr, 1); lkv("taken", (uint32_t)tk, 1); lkv("rx", (uint32_t)w, 1);
        if (w == 1) { uint32_t st = g_cur.d[28] | g_cur.d[29] << 8; lkv("status", st, 4); if (st == 0) auth = 1; }
        else if (w < 0) lkv("deauth_reason", g_dauth, 2);
        else { lput(" "); qlog(); }
        lput("\r\n");
    }
    if (!auth) { lput("WIFI.conn res=fail stage=auth\r\n"); g_listen = 0; return 0; }
    int assoc = 0;
    for (int tr = 0; tr < 4 && !assoc; tr++) {
        int n = 0; for (int i = 0; i < 24; i++) fr[i] = 0;
        fr[0] = 0x00; for (int j = 0; j < 6; j++) { fr[4 + j] = g_bssid[j]; fr[10 + j] = g_mac[j]; fr[16 + j] = g_bssid[j]; }
        n = 24; fr[n++] = 0x11; fr[n++] = 0x04; fr[n++] = 0x0A; fr[n++] = 0;                        /* 능력(ESS|privacy|짧은슬롯), 청취간격 */
        fr[n++] = 0; fr[n++] = (uint8_t)sl; for (int i = 0; i < sl; i++) fr[n++] = (uint8_t)g_ssid[i];
        static const uint8_t rates[] = { 1, 8, 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24, 50, 4, 0x30, 0x48, 0x60, 0x6C };
        for (unsigned i = 0; i < sizeof rates; i++) fr[n++] = rates[i];   /* 표준대로 지원 속도 전체(기본 8 + 확장 4) 를 알림 */
        for (int i = 0; i < g_rsniel; i++) fr[n++] = g_rsnie[i];
        int tk = tx_mgmt(m, fr, (uint32_t)n, 0);
        int w = wait_mgmt(m, 0x10, 800);
        lput("WIFI.assoc"); lkv("try", (uint32_t)tr, 1); lkv("taken", (uint32_t)tk, 1); lkv("rx", (uint32_t)w, 1);
        if (w == 1) { uint32_t st = g_cur.d[26] | g_cur.d[27] << 8; g_aid = (uint16_t)((g_cur.d[28] | g_cur.d[29] << 8) & 0x3FFF); lkv("status", st, 4); lkv("aid", g_aid, 4); if (st == 0) assoc = 1; }
        else if (w < 0) lkv("deauth_reason", g_dauth, 2);
        else { lput(" "); qlog(); }
        lput("\r\n");
    }
    if (!assoc) { lput("WIFI.conn res=fail stage=assoc\r\n"); g_listen = 0; return 0; }
    /* 연결 상태를 칩에 알림: BSSID, 네트워크 종류(infra) — 펌웨어 H2C 는 아직 안 보냄 */
    R32(0x618) = g_bssid[0] | g_bssid[1] << 8 | g_bssid[2] << 16 | (uint32_t)g_bssid[3] << 24; R16(0x61C) = (uint16_t)(g_bssid[4] | g_bssid[5] << 8);
    R32(0x100) = (R32(0x100) & ~0x30000u) | 0x20000u;
    lput("WIFI.vif"); lkv("r618", R32(0x618), 8); lkv("r61c", R16(0x61C), 4); lkv("r100", R32(0x100), 8); lput("\r\n");
    int ok4 = do_4way(m);
    if (!ok4) { lput("WIFI.conn res=fail stage=4way\r\n"); g_listen = 0; return 0; }
    if (g_live) {    /* DHCP·ARP 는 lwIP(netstack.c)가 한다. 여기서는 링크만 올린다 */
        for (int j = 0; j < 6; j++) g_lb[j] = g_bssid[j];
        g_lastbcn = tms(); g_lbon = 1; g_linked = 1;
        lput("WIFI.conn res=ok live=1 ssid="); put_ssid((const uint8_t *)g_ssid, sl); lkv("ch", g_chan, 2); lput("\r\n");
        return 1;
    }
    /* DHCP */
    uint32_t xid = 0x58363530u ^ (uint32_t)__builtin_ia32_rdtsc(); int got = 0;
    for (int tr = 0; tr < 4 && !got; tr++) {
        int tk = dhcp_send(m, 1, xid, 0, 0, 0); lput("WIFI.dhcp discover"); lkv("try", (uint32_t)tr, 1); lkv("taken", (uint32_t)tk, 1); lput("\r\n");
        got = dhcp_wait(m, xid, 2, 2500);
    }
    if (!got) { lput("WIFI.dhcp res=fail "); qlog(); lput("\r\n"); lput("WIFI.conn res=fail stage=dhcp_offer\r\n"); g_listen = 0; return 0; }
    uint8_t offer_ip[4], offer_srv[4]; for (int j = 0; j < 4; j++) { offer_ip[j] = g_myip[j]; offer_srv[j] = g_srv[j]; }
    int ack = 0;
    for (int tr = 0; tr < 3 && !ack; tr++) { int tk = dhcp_send(m, 3, xid, offer_ip, offer_srv, tr); lput("WIFI.dhcp request"); lkv("variant", (uint32_t)tr, 1); lkv("taken", (uint32_t)tk, 1); lput("\r\n"); ack = dhcp_wait(m, xid, 5, 2500); }
    if (!ack) {   /* 나중 프레임 송신이 되는지 구분 시험: ARP 탐지(출발지 0.0.0.0) + discover 다시 */
        int pr = arp_gw(m, 1); lput("WIFI.arpprobe res="); lput(pr ? "reply" : "none"); lput("\r\n");
        uint32_t x2 = xid + 0x1000; int tk2 = dhcp_send(m, 1, x2, 0, 0, 0); int o2 = dhcp_wait(m, x2, 2, 2000);
        lput("WIFI.redisc taken="); lhex((uint32_t)tk2, 1); lput(" offer="); lput(o2 ? "yes" : "none"); lput("\r\n");
        for (int j = 0; j < 4; j++) { g_myip[j] = offer_ip[j]; g_srv[j] = offer_srv[j]; }
    }
    if (!ack) { lput("WIFI.dhcp res=fail "); qlog(); lput("\r\n"); lput("WIFI.dhcp note=ACK_none_using_offered_ip_for_diag\r\n"); for (int j = 0; j < 4; j++) g_myip[j] = offer_ip[j]; }
    if (!g_gwip[0] && !g_gwip[1]) for (int j = 0; j < 4; j++) g_gwip[j] = offer_srv[j];
    int gw = arp_gw(m, 0); int pg = gw ? ping_gw(m) : 0;
    lput("WIFI.conn res="); lput(pg ? (ack ? "ok" : "ok_noack") : (gw ? "partial why=ping_no_reply" : "partial why=arp_no_reply")); lput(" ip="); lip(g_myip); lput(" gw="); lip(g_gwip); lput("\r\n");
    for (int i = 0; i < 26; i++) fr[i] = 0;
    fr[0] = 0xC0; for (int j = 0; j < 6; j++) { fr[4 + j] = g_bssid[j]; fr[10 + j] = g_mac[j]; fr[16 + j] = g_bssid[j]; } fr[24] = 3;
    tx_mgmt(m, fr, 26, 0); pump(m, 50);
    g_listen = 0;
    return pg;
}
static int g_wst;   /* 0 대기(공유기 미지정) 1 연결 중 2 연결됨 3 공유기를 못 찾음 4 접속 실패(암호?) */
/* 실시간 모드의 스캔+접속. 2.4GHz 1~13번만, 마지막에 붙은 채널부터 듣고 목표 SSID(RSN)를 보면 바로 멈춘다. 코루틴 안에서 돌면 기다리는 동안 MSX 가 계속 돈다 */
static int rtw_live_connect(volatile uint8_t *m, struct rtw_dev *d)
{
    int sl = 0; while (g_ssid[sl]) sl++;
    if (!sl) { lput("WIFI.conn res=skip why=no_SSID_in_RTW.INI\r\n"); return 0; }
    nbss = 0; g_linked = 0; g_lbon = 0; g_wst = 1;
    uint8_t order[13]; int no = 0;
    if (g_chan >= 1 && g_chan <= 13) order[no++] = g_chan;
    for (uint8_t c = 1; c <= 13; c++) if (c != g_chan) order[no++] = c;
    int found = 0;
    for (int ci = 0; ci < no && !found; ci++) {
        rtw8822c_set_channel(d, order[ci], RTW_CHANNEL_WIDTH_20, 0);
        uint32_t rx = 0, bc = 0, c2 = 0, bad = 0, hw = 0;
        pumpx(m, 300, order[ci], &rx, &bc, &c2, &bad, &hw);
        for (int i = 0; i < nbss; i++) if (bss[i].ssidn == sl && !__builtin_memcmp(bss[i].ssid, g_ssid, (unsigned)sl) && (bss[i].sec & 2) && bss[i].ch < 14) { found = 1; break; }
    }
    if (!g_quiet) { lput("WIFI.lscan"); lkv("aps", (uint32_t)nbss, 2); lkv("found", (uint32_t)found, 1); lput("\r\n"); }
    if (!found) { g_wst = 3; return 0; }
    int okc = rtw_connect(m, d);
    g_wst = okc ? 2 : 4;
    return okc;
}
static int rtw_scan(volatile uint8_t *m, void *dma)
{
    if (!dma || g_dmasz < 12288 + RXN * RXBUF + 4096 + 2 * TXN * 768) { lput("WIFI.scan res=skip why=dma_small\r\n"); return 0; }
    struct rtw_dev *d = &g_rd; d->m = m;
    uint8_t *base = (uint8_t *)dma; uint8_t *bd = base + 8192; uint8_t *bufs = base + 12288; g_bd = bd; g_bufs = bufs;
    g_txbd[0] = base + 12288 + RXN * RXBUF; g_txbd[1] = g_txbd[0] + 256; g_txpk[0] = g_txbd[0] + 4096; g_txpk[1] = g_txpk[0] + TXN * 768;
    for (int i = 0; i < RXN; i++) {
        uint8_t *e = bd + i * 8; *(uint16_t *)e = RXBUF; *(uint16_t *)(e + 2) = 0; *(uint32_t *)(e + 4) = (uint32_t)(uintptr_t)(bufs + i * RXBUF);
        *(volatile uint32_t *)(bufs + i * RXBUF) = 0; *(volatile uint32_t *)(bufs + i * RXBUF + 8) = 0;
    }
    for (int i = 0; i < 2 * 256; i++) g_txbd[0][i] = 0;
    for (int i = 0; i < 6; i++) g_mac[i] = ef_log[0x120 + i];
    R8(0x303) |= 0xF7;
    R32(0x338) = (uint32_t)(uintptr_t)bd; R16(0x382) = RXN & 0xFFF;
    R32(0x310) = (uint32_t)(uintptr_t)g_txbd[0]; R16(0x380) = TXN & 0xFFF;         /* MGMT 큐 링 */
    R32(0x328) = (uint32_t)(uintptr_t)g_txbd[1]; R16(0x388) = TXN & 0xFFF;         /* BE 큐 링 (데이터) */
    R32(0x39C) = 0xFFFFFFFFu;
    R32(0x300) |= (1u << 20) | (1u << 15);
    R16(0x3B4) = 0; g_rp = 0; g_txwp_q[0] = g_txwp_q[1] = 0;
    R32(0x610) = g_mac[0] | g_mac[1] << 8 | g_mac[2] << 16 | (uint32_t)g_mac[3] << 24; R16(0x614) = (uint16_t)(g_mac[4] | g_mac[5] << 8);   /* 내 MAC (REG_MACID) */
    lput("WIFI.rxring res=ok"); lkv("bd", (uint32_t)(uintptr_t)bd, 8); lkv("n", RXN, 3); lkv("r338", R32(0x338), 8); lkv("r382", R16(0x382), 4); lkv("r3b4", R32(0x3B4), 8);
    lkv("r608", R32(0x608), 8); lkv("r6a0", R16(0x6A0), 4); lkv("r6a2", R16(0x6A2), 4); lkv("r6a4", R16(0x6A4), 4); lkv("r60f", R8(0x60F), 2);
    lkv("txbd", (uint32_t)(uintptr_t)g_txbd[0], 8); lkv("r310", R32(0x310), 8); lkv("r380", R16(0x380), 4); lkv("r3b0", R32(0x3B0), 8); lkv("r610", R32(0x610), 8); lkv("r614", R16(0x614), 4); lput("\r\n");
    rtw_coexfix(m);
    g_ready = 1;
    if (g_live) { rtw_live_connect(m, d); return 1; }   /* 실시간 모드는 짧은 스캔 + 접속만 (진단용 전체 스캔·송신 시험은 건너뜀) */
    static const uint8_t chs[] = { 1,2,3,4,5,6,7,8,9,10,11,12,13, 36,40,44,48,52,56,60,64, 100,104,108,112,116,120,124,128,132,136,140, 149,153,157,161,165 };
    uint32_t tot = 0, totb = 0, totc = 0, totbad = 0;
    for (unsigned ci = 0; ci < sizeof chs; ci++) {
        rtw8822c_set_channel(d, chs[ci], RTW_CHANNEL_WIDTH_20, 0);
        uint32_t rx = 0, bc = 0, c2 = 0, bad = 0, hw = 0;
        pumpx(m, chs[ci] < 14 ? 450 : 250, chs[ci], &rx, &bc, &c2, &bad, &hw);
        tot += rx; totb += bc; totc += c2; totbad += bad;
        if (rx || ci < 3) { lput("WIFI.scan"); lkv("ch", chs[ci], 2); lkv("rx", rx, 3); lkv("bcn", bc, 3); lkv("c2h", c2, 2); lkv("bad", bad, 2); lkv("hw", hw, 3); lput("\r\n"); }
    }
    lput("WIFI.scan res=ok"); lkv("rx", tot, 4); lkv("bcn", totb, 4); lkv("c2h", totc, 3); lkv("bad", totbad, 3); lkv("aps", (uint32_t)nbss, 2); lkv("r3b4", R32(0x3B4), 8); lput("\r\n");
    for (int i = 0; i < nbss; i++) {
        lput("WIFI.bss"); lput(" bssid="); for (int j = 0; j < 6; j++) { lhex(bss[i].bssid[j], 2); if (j < 5) lput(":"); }
        lkv("ch", bss[i].ch, 2); lkv("sec", bss[i].sec, 1); lkv("rate", bss[i].rate, 2); lkv("n", bss[i].cnt, 3); lput(" ssid="); put_ssid(bss[i].ssid, bss[i].ssidn); lput("\r\n");
    }
    /* 4b: probe request 송신 시험 (2.4GHz 1·6·11번, 1Mbps CCK, 와일드카드 SSID) */
    static const uint8_t pch[3] = { 1, 6, 11 };
    uint8_t fr[40]; for (int i = 0; i < 40; i++) fr[i] = 0;
    fr[0] = 0x40; for (int j = 0; j < 6; j++) { fr[4 + j] = 0xFF; fr[10 + j] = g_mac[j]; fr[16 + j] = 0xFF; }
    fr[24] = 0; fr[25] = 0; fr[26] = 1; fr[27] = 4; fr[28] = 0x82; fr[29] = 0x84; fr[30] = 0x8B; fr[31] = 0x96;
    for (int pi = 0; pi < 3; pi++) {
        rtw8822c_set_channel(d, pch[pi], RTW_CHANNEL_WIDTH_20, 0);
        rtw_txpwr2g(m, pch[pi]);
        int sent = 0, taken = 0; uint32_t rx = 0, bc = 0, c2 = 0, bad = 0, hw = 0, pr0 = g_prsp;
        for (int rep = 0; rep < 4; rep++) {
            sent++; if (tx_mgmt(m, fr, 32, 0)) taken++;
            pumpx(m, 150, pch[pi], &rx, &bc, &c2, &bad, &hw);
        }
        lput("WIFI.tx"); lkv("ch", pch[pi], 2); lkv("sent", (uint32_t)sent, 1); lkv("taken", (uint32_t)taken, 1); lkv("rx", rx, 3); lkv("prsp", g_prsp - pr0, 3); lkv("c2h", c2, 2); lkv("r3b0", R32(0x3B0), 8); lkv("r3b4", R32(0x3B4), 8); lput("\r\n");
    }
    lput("WIFI.tx res=ok"); lkv("prsp_total", g_prsp, 3); lput("\r\n");
    rtw8822c_set_channel(d, 6, RTW_CHANNEL_WIDTH_20, 0);
    rtw_connect(m, d);
    return 1;
}

static int rtw_dlfw(volatile uint8_t *m, const uint8_t *fw, uint32_t fwsz, void *dma)
{
    uint8_t *ring = (uint8_t *)dma, *pk = (uint8_t *)dma + 4096;
    const char *why = "";
    uint32_t dm = *(const uint32_t *)(fw + 0x24) + 8, im = *(const uint32_t *)(fw + 0x30) + 8, em = *(const uint32_t *)(fw + 0x34);
    if (!(fw[0x18] & 0x10)) em = 0; else em += 8;
    (void)fwsz;
    dl_cpu(m, 0);
    uint8_t b_pq = R8(0x10D), b_cr = R8(0x100);
    uint16_t b_fi = R16(0x230); uint32_t b_rq = R32(0x22C) | (1u << 31); uint8_t b_bc = R8(0x550);
    R8(0x10D) = 3 << 6; R8(0x100) = 0x05; R32(0x1330) = 1u << 31;
    R16(0x230) = 0x200; R32(0x22C) = b_rq;
    R8(0x550) = (uint8_t)((b_bc & ~0x08) | 0x10);
    R8(0x1082) &= (uint8_t)~1; R8(0x09) &= (uint8_t)~0x40; R8(0x1082) |= 1; R8(0x09) |= 0x40;   /* 플랫폼 리셋 */
    lput("WIFI.dl0 res=ok"); lkv("r80", R32(0x80), 8); lkv("r1208", R32(0x1208), 8); lput("\r\n");
    R16(0x80) = (uint16_t)((R16(0x80) & 0x3800) | 1);            /* MCUFWDL_EN */
    int ok = 1;
    const uint8_t *cur = fw + 64;
    uint32_t addr[3] = { *(const uint32_t *)(fw + 0x20) & 0x7FFFFFFFu, *(const uint32_t *)(fw + 0x3C) & 0x7FFFFFFFu, *(const uint32_t *)(fw + 0x38) & 0x7FFFFFFFu };
    uint32_t len[3] = { dm, im, em };
    static const char nm[3] = { 'D', 'I', 'E' };
    for (int i = 0; i < 3 && ok; i++) {
        if (!len[i]) continue;
        ok = dl_seg(m, ring, pk, cur, addr[i], len[i], &why);
        lput("WIFI.dl res="); lput(ok ? "ok" : "fail"); lput(" seg="); { char c[2] = { nm[i], 0 }; lput(c); }
        lkv("dst", addr[i], 8); lkv("len", len[i], 8); lkv("r1208", R32(0x1208), 8); lkv("r80", R32(0x80), 8); if (!ok) { lput(" why="); lput(why); } lput("\r\n");
        cur += len[i];
    }
    R8(0x10D) = b_pq; R8(0x100) = b_cr; R32(0x1330) = 1u << 31; R16(0x230) = b_fi; R32(0x22C) = b_rq; R8(0x550) = b_bc;   /* 레지스터 복원 */
    if (!ok) { R8(0x80) &= (uint8_t)~1; R8(0x03) |= 4; return 0; }
    R32(0x210) = 1u << 2;
    uint16_t fc = R16(0x80);
    if ((fc & 0x50) == 0x50) { fc = (uint16_t)((fc | 0x4000) & ~1); R16(0x80) = fc; }
    dl_cpu(m, 1);
    int rdy = pollm(m, 0x80, 0xCFFF, 0x8000 | 0x4000 | 0x08 | 0x20 | 0x50, 1000000);
    lput("WIFI.fwrdy res="); lput(rdy ? "ok" : "fail"); lkv("r80", R32(0x80), 8); lkv("rFD", R8(0xFD), 2); lput("\r\n");
    if (!rdy) { R8(0x80) &= (uint8_t)~1; R8(0x03) |= 4; }
    else if (rtw_trxcfg(m)) { if (rtw_macinit(m, (int)((R32(0xF0) >> 12) & 0xF)) && rtw_phytbl(m) && rtw_trxmode(m) && rtw_rfinit(m)) { rtw_chan(m); rtw_scan(m, dma); } }
    return rdy;
}

/* 장치 위쪽 다리(PCI-PCI bridge)의 버스마스터/메모리 허용을 켠다 (UEFI 는 드라이버가 안 붙은 장치의 다리는 안 켜 둠).
 * 5.0.139 로그: 상태에 Received Master Abort + 장치 DevSta 에 Unsupported Request -> 칩의 메모리 읽기가 다리에서 거절됨 */
typedef struct { uint8_t b, d, f; uint16_t cmd; } BrgSave;
static BrgSave brg[4]; static int nbrg;
static void brg_on(int B)
{
    nbrg = 0;
    for (int tgt = B; tgt > 0 && nbrg < 4;) {
        int found = 0;
        for (int b = 0; b < tgt && !found; b++)
            for (int d = 0; d < 32 && !found; d++)
                for (int f = 0; f < 8 && !found; f++) {
                    uint32_t id = cr(b, d, f, 0); if (id == 0xFFFFFFFFu || id == 0) { if (f == 0) break; continue; }
                    if (((cr(b, d, f, 8) >> 16) & 0xFFFF) != 0x0604) continue;
                    uint32_t bn = cr(b, d, f, 0x18); int sec = (int)((bn >> 8) & 0xFF), sub = (int)((bn >> 16) & 0xFF);
                    if (tgt < sec || tgt > sub) continue;
                    uint32_t c = cr(b, d, f, 4);
                    brg[nbrg].b = (uint8_t)b; brg[nbrg].d = (uint8_t)d; brg[nbrg].f = (uint8_t)f; brg[nbrg].cmd = (uint16_t)c; nbrg++;
                    lput("WIFI.brg"); lkv("bus", (uint32_t)b, 2); lkv("dev", (uint32_t)d, 2); lkv("fn", (uint32_t)f, 1); lkv("sec", (uint32_t)sec, 2); lkv("sub", (uint32_t)sub, 2); lkv("cmd_before", c & 0xFFFF, 4); lkv("sts", c >> 16, 4); lput("\r\n");
                    cw(b, d, f, 4, (c & 0xFFFFu) | 0x0006u);                 /* 상태 비트는 건드리지 않음 */
                    tgt = b; found = 1;
                }
        if (!found) break;
    }
}
static void brg_off(void) { for (int i = 0; i < nbrg; i++) cw(brg[i].b, brg[i].d, brg[i].f, 4, brg[i].cmd); nbrg = 0; }

static int g_pm, g_Bs, g_Ds; static uint32_t g_cmd0, g_pmcsr0; static volatile uint8_t *g_m8; static const uint8_t *g_fw; static uint32_t g_fwsz; static void *g_dma; static int g_dl;
/* 세 토막으로 나눔 - begin(PCI 설정 접근, 메인 코어) → work(칩 메모리만 만짐, 어느 코어든 가능) → end(PCI 설정 복구, 메인 코어) */
int rtwdet_begin(char *buf, int max, const uint8_t *fw, uint32_t fwsz, void *dma, uint32_t dmasz, int dl, const uint8_t *phy, uint32_t phyn)
{
    g_phy = phy; g_phyn = phyn;
    lg = buf; lgn = 0; lgmax = max; buf[0] = 0;
    int B = -1, D = 0;
    for (int b = 0; b < 16 && B < 0; b++)
        for (int d = 0; d < 32; d++)
            if (cr(b, d, 0, 0) == 0xC82210ECu) { B = b; D = d; break; }
    if (B < 0) { lput("WIFI.find res=fail why=C822_not_found\r\n"); return 0; }
    uint32_t cmd0 = cr(B, D, 0, 4) & 0xFFFFu, bar2 = cr(B, D, 0, 0x18), bar3 = cr(B, D, 0, 0x1C);
    int pm = 0;
    if (cr(B, D, 0, 4) & 0x00100000u) {
        int p = (int)(cr(B, D, 0, 0x34) & 0xFC);
        for (int i = 0; i < 16 && p >= 0x40; i++) { uint32_t h = cr(B, D, 0, p); if ((h & 0xFF) == 0x01) { pm = p; break; } p = (int)((h >> 8) & 0xFC); }
    }
    uint32_t pmcsr0 = pm ? (cr(B, D, 0, pm + 4) & 0xFFFFu) : 0;
    lput("WIFI.find res=ok"); lkv("bus", (uint32_t)B, 2); lkv("dev", (uint32_t)D, 2); lkv("cmd", cmd0, 4); lkv("bar2", bar2, 8); lkv("bar3", bar3, 8); lkv("pm", (uint32_t)pm, 2); lkv("pmcsr", pmcsr0, 4); lput("\r\n");
    g_B = B; g_D = D; g_pcie = 0; g_dmasz = dmasz;
    if (cr(B, D, 0, 4) & 0x00100000u) { int p = (int)(cr(B, D, 0, 0x34) & 0xFC); for (int i = 0; i < 16 && p >= 0x40; i++) { uint32_t h = cr(B, D, 0, p); if ((h & 0xFF) == 0x10) { g_pcie = p; break; } p = (int)((h >> 8) & 0xFC); } }
    uint64_t base = (bar2 & 6) == 4 ? (((uint64_t)bar3 << 32) | (bar2 & 0xFFFFFFF0u)) : (uint64_t)(bar2 & 0xFFFFFFF0u);
    if ((bar2 & 1) || !base) { lput("WIFI.bar res=fail why=BAR2_unassigned_or_io\r\n"); return 0; }
    if (pm && (pmcsr0 & 3)) { cw(B, D, 0, pm + 4, pmcsr0 & ~0x8003u & 0xFFFFu); wait_ms(10); }
    brg_on(B);
    cw(B, D, 0, 4, cmd0 | 0x0006u); wait_ms(10);
    cw(B, D, 0, 4, (cmd0 | 0x0006u) | 0xF9000000u); /* 이전 상태 오류 비트(쓰면 지워짐) 정리 */ wait_ms(1);
    volatile uint8_t *m8 = (volatile uint8_t *)(uintptr_t)base;
    g_pm = pm; g_Bs = B; g_Ds = D; g_cmd0 = cmd0; g_pmcsr0 = pmcsr0; g_m8 = m8; g_fw = fw; g_fwsz = fwsz; g_dma = dma; g_dl = dl;
    return 1;
}

void rtwdet_work(void)
{
    volatile uint8_t *m8 = g_m8; volatile uint32_t *m32 = (volatile uint32_t *)g_m8;
    const uint8_t *fw = g_fw; uint32_t fwsz = g_fwsz; void *dma = g_dma; uint32_t dmasz = g_dmasz; int dl = g_dl;
    uint32_t f0 = m32[0xF0 / 4], fc = m32[0xFC / 4], cr0 = m32[0x100 / 4];
    lput("WIFI.regs1 res="); lput(f0 == 0xFFFFFFFFu ? "fail" : "ok"); lkv("F0", f0, 8); lkv("FC", fc, 8); lkv("CR", cr0, 8);
    lkv("r05", m8[5], 2); lkv("r06", m8[6], 2); lput("\r\n");
    if (f0 == 0xFFFFFFFFu) { lput("WIFI.pwr res=skip why=mmio_all_FF\r\n"); return; }
    /* 사전 설정 (rtw_mac_pre_system_cfg, PCIe): RSV_CTRL=0, HCI_OPT_CTRL |= BIT8 */
    m8[0x1C] = 0;
    m32[0x74 / 4] |= 0x100u;
    int fail = -1;
    for (int i = 0; i < (int)(sizeof pwr_on / sizeof pwr_on[0]) && fail < 0; i++) {
        const PwrCmd *c = &pwr_on[i];
        if (c->cmd == 1) { uint8_t v = m8[c->off]; v = (uint8_t)((v & ~c->mask) | (c->val & c->mask)); m8[c->off] = v; }
        else {
            int ok = 0;
            for (int t = 0; t < 2000; t++) { if ((m8[c->off] & c->mask) == (c->val & c->mask)) { ok = 1; break; } wait_ms(0); for (int u = 0; u < 50; u++) p_outb(0x80, 0); }
            if (!ok) fail = i;
        }
    }
    lput("WIFI.pwr res="); lput(fail < 0 ? "ok" : "fail"); if (fail >= 0) lkv("step", (uint32_t)fail, 2); lput("\r\n");
    wait_ms(10);
    lput("WIFI.regs2 res=ok"); lkv("F0", m32[0xF0 / 4], 8); lkv("FC", m32[0xFC / 4], 8); lkv("CR", m32[0x100 / 4], 8);
    lkv("r05", m8[5], 2); lkv("r06", m8[6], 2); lkv("r1018", m8[0x1018], 2); lkv("r1064", m8[0x1064], 2); lput("\r\n");
    if (fail < 0) {
        /* 2a단계 (DMA 없음) - 펌웨어 파일 머리말 검사 + MAC 시스템 설정(rtw_mac_init_system_cfg 순서) */
        if (!fw || fwsz < 64) lput("WIFI.fw res=skip why=no_file DRV\\rtw8822c_fw.bin\r\n");
        else {
            uint32_t dm = *(const uint32_t *)(fw + 0x24), im = *(const uint32_t *)(fw + 0x30), em = *(const uint32_t *)(fw + 0x34);
            uint8_t mu = fw[0x18]; if (!(mu & 0x10)) em = 0;
            uint32_t real = 64 + dm + 8 + im + 8 + (em ? em + 8 : 0);
            lput("WIFI.fw res="); lput((fw[0] == 0x22 && fw[1] == 0x88 && real == fwsz) ? "ok" : "fail");
            lkv("size", fwsz, 8); lkv("calc", real, 8); lkv("sig", (uint32_t)(fw[0] | fw[1] << 8), 4); lkv("ver", (uint32_t)(fw[4] | fw[5] << 8), 4);
            lkv("sub", fw[6], 2); lkv("idx", fw[7], 2); lkv("mu", mu, 2); lkv("dmem", *(const uint32_t *)(fw + 0x20), 8); lkv("imem", *(const uint32_t *)(fw + 0x3C), 8); lkv("emem", *(const uint32_t *)(fw + 0x38), 8); lput("\r\n");
        }
        uint32_t v = m32[0x1080 / 4]; v |= 0x10000u | 0x100u; m32[0x1080 / 4] = v;     /* WL_PLATFORM_RST | DDMA_EN */
        m8[3] |= 0xD8;                                                                  /* SYS_FUNC_EN+1 |= sys_func_en(8822C) */
        m8[0x1103] = (uint8_t)((m8[0x1103] & 0xF0) | 0x0C);                              /* CR_EXT+3 */
        v = m32[0x80 / 4];
        if (v & 0x100000u) { m32[0x80 / 4] = v & ~0x100000u; m32[0x40 / 4] &= ~0x80000u; }   /* flash 부팅 끄기 */
        wait_ms(5);
        lput("WIFI.mac res=ok"); lkv("r1080", m32[0x1080 / 4], 8); lkv("r02", m8[2], 2); lkv("r03", m8[3], 2); lkv("r1103", m8[0x1103], 2); lkv("r80", m32[0x80 / 4], 8); lkv("r40", m32[0x40 / 4], 8); lput("\r\n");
        rtw_efuse(m8); g_xtal = ef_log[0xB9];                                  /* 3c - 리눅스처럼 펌웨어 올리기 전에 efuse 읽기 */
        /* 2b-1단계 (전송 없음) - 신호등 링 주소만 칩에 쓰고 읽어 확인. rtw_pci_reset_buf_desc 의 BCN 줄과 같음 */
        if (!dma || dmasz < 4096 || ((uintptr_t)dma >> 32) || dmasz < 12288) lput("WIFI.ring res=skip why=no_dma_below_4G\r\n");
        else {
            volatile uint8_t *rg = (volatile uint8_t *)dma; for (uint32_t i = 0; i < 4096; i++) rg[i] = 0;
            uint32_t pa = (uint32_t)(uintptr_t)dma;
            m8[0x303] |= 0xF7;                         /* RTK_PCI_CTRL+3 */
            m32[0x308 / 4] = pa;                       /* TXBD_DESA_BCNQ */
            m32[0x39C / 4] = 0xFFFFFFFFu;              /* TXBD_RWPTR_CLR */
            m32[0x300 / 4] |= (1u << 20) | (1u << 15);   /* rtw_pci_dma_reset - RST_TRXDMA_INTF | RX_TAG_EN */
            wait_ms(1);
            uint32_t rb = m32[0x308 / 4];
            lput("WIFI.ring res="); lput(rb == pa ? "ok" : "fail"); lkv("pa", pa, 8); lkv("desa", rb, 8); lkv("ctrl3", m8[0x303], 2); lkv("idx", m32[0x39C / 4], 8); lput("\r\n");
            if (dl && rb == pa && fw && fwsz >= 64 && fw[0] == 0x22 && fw[1] == 0x88) rtw_dlfw(m8, fw, fwsz, dma);
            else lput("WIFI.dl res=skip why=not_requested_or_ring_fail\r\n");
        }
    }
}

int rtwdet_end(void)
{
    int B = g_Bs, D = g_Ds, pm = g_pm; uint32_t cmd0 = g_cmd0, pmcsr0 = g_pmcsr0;
    brg_off();
    cw(B, D, 0, 4, cmd0);                                     /* PCI CMD 원상 복구 (칩 전원은 그대로) */
    if (pm && (pmcsr0 & 3)) cw(B, D, 0, pm + 4, pmcsr0 & 0x7FFFu);
    lput("WIFI.end res=ok note=pci_cmd_restored\r\n");
    return lgn;
}

int rtwdet_power(char *buf, int max, const uint8_t *fw, uint32_t fwsz, void *dma, uint32_t dmasz, int dl, const uint8_t *phy, uint32_t phyn)
{
    if (!rtwdet_begin(buf, max, fw, fwsz, dma, dmasz, dl, phy, phyn)) return lgn;
    rtwdet_work();
    return rtwdet_end();
}

int rtwdet_loglen(void) { return lgn; }
void rtwdet_log(const char *s) { lput(s); }

/* ====== 5.0.157: Nic 인터페이스 (kernel/net/nic.h) — netstack.c(lwIP)가 DHCP·ARP·TCP/UDP 를 맡고, 여기서는 이더넷 프레임 ↔ 802.11 데이터 프레임(LLC/SNAP + CCMP)만 바꿈 ====== */
#include "../kernel/net/nic.h"
static int g_pre, g_txfail; static uint32_t g_relink;
/* 5.2.91 진단: 집 공유기에서 DHCP 응답을 못 받는 문제 — 받은 데이터 프레임이 어디서 버려지는지 기록 (동작은 바꾸지 않음) */
static uint8_t g_dr_last = 4;
static uint32_t s_rxd, s_dec[8], s_bss, s_llc, s_et, s_self, s_pass, s_dhrx, s_dhtx, s_logn, s_txlog, s_stat_t;
static uint32_t s_optlog;
static void dhcp_optlog(const char *tag, const uint8_t *ip, int len)   /* 5.2.98 진단: DHCP 메시지의 플래그·옵션 원문 (REQUEST 에 서버 식별자·요청 주소가 제대로 들어갔나, OFFER 를 제대로 읽었나) */
{
    if (s_optlog >= 12 || len < 20 + 8 + 240 + 4) return;
    s_optlog++;
    int o = 20 + 8;                                     /* IP 머리(20) + UDP(8) 다음이 BOOTP */
    lput("WIFI.dhcp opt "); lput(tag); lput(" flags="); lhex((uint32_t)(ip[o + 10] << 8 | ip[o + 11]), 4); lput(" ci="); lhex((uint32_t)(ip[o + 12] << 24 | ip[o + 13] << 16 | ip[o + 14] << 8 | ip[o + 15]), 8);
    lput(" len="); lhex((uint32_t)len, 3); lput(" o="); int m = len - (o + 240); if (m > 48) m = 48;
    for (int j = 0; j < m; j++) { lhex(ip[o + 240 + j], 2); lput(" "); }
    lput("\r\n");
}
static int dhcp_type(const uint8_t *ip, int len)       /* IPv4 패킷이 DHCP 면 메시지 종류(1 DISCOVER 2 OFFER 3 REQUEST 5 ACK 6 NAK), 아니면 0 */
{
    if (len < 20 || (ip[0] >> 4) != 4 || ip[9] != 17) return 0;
    int ihl = (ip[0] & 15) * 4; if (len < ihl + 8 + 240) return 0;
    const uint8_t *u = ip + ihl; int sp = u[0] << 8 | u[1], dp = u[2] << 8 | u[3];
    if (!((sp == 67 && dp == 68) || (sp == 68 && dp == 67))) return 0;
    const uint8_t *o = u + 8 + 240, *e = ip + len;
    while (o + 2 <= e && *o != 255) { if (*o == 0) { o++; continue; } if (*o == 53 && o[1] >= 1 && o + 3 <= e) return o[2]; o += 2 + o[1]; }
    return 9;                                          /* DHCP 이지만 종류 못 찾음 */
}
static void rx_log(int mc, int n)                      /* 받은 데이터 프레임 한 줄 (연결마다 앞쪽 40개) */
{
    if (s_logn >= 40) return;
    s_logn++;
    lput("WIFI.rx"); lkv("mc", (uint32_t)mc, 1); lkv("prot", (uint32_t)((g_cur.d[1] >> 6) & 1), 1); lkv("dec", g_dec, 1); lkv("n", (uint32_t)n, 3);
    if (n >= 8) { lkv("et", (uint32_t)(g_pl[6] << 8 | g_pl[7]), 4);
        if (n >= 8 + 20 && g_pl[6] == 8 && g_pl[7] == 0) { const uint8_t *ip = g_pl + 8; int ihl = (ip[0] & 15) * 4; lkv("pr", ip[9], 2);
            if (ip[9] == 17 && n >= 8 + ihl + 4) { lkv("sp", (uint32_t)(ip[ihl] << 8 | ip[ihl + 1]), 4); lkv("dp", (uint32_t)(ip[ihl + 2] << 8 | ip[ihl + 3]), 4); } } }
    lput(" da="); lmac(g_cur.d + 4); lput("\r\n");
}
static void rx_stat(void)                              /* 10초마다 요약 */
{
    if ((int32_t)(tms() - s_stat_t) < 10000) return;
    s_stat_t = tms();
    lput("WIFI.stat"); lkv("q", g_qseen, 5); lkv("qdrop", g_qdrop, 4); lkv("rxd", s_rxd, 5); lkv("bss", s_bss, 4);
    lkv("plain", s_dec[1], 4); lkv("ccmp", s_dec[2], 4); lkv("ccmpbad", s_dec[3], 4); lkv("tkip", s_dec[4], 4); lkv("tkipbad", s_dec[5], 4); lkv("nokey", s_dec[6], 4);
    lkv("llc", s_llc, 4); lkv("et", s_et, 4); lkv("self", s_self, 3); lkv("pass", s_pass, 5); lkv("dhcptx", s_dhtx, 3); lkv("dhcprx", s_dhrx, 3); lkv("grp", g_grp3, 1); lput("\r\n");
    /* 5.2.97 진단: 보낸 프레임이 공유기에 닿는가 — 속도별(CCK1M=0 CCK11M=3 OFDM6M=4) 보고 건수 n, 재시도 평균 avg, 한계(15번)까지 간 건수 mx. mx 가 n 에 가까우면 공유기가 못 듣는다(출력·거리·안테나). bcnrate=받은 비콘의 속도, bcn=받은 비콘 수, bcnage=마지막 비콘 후 ms */
    lput("WIFI.txq"); for (int i = 0; i < 3; i++) { lput(i == 0 ? " r0" : i == 1 ? " r3" : " r4"); lkv("n", g_txr_n[i], 3); lkv("avg", g_txr_n[i] ? g_txr_b8[i] / g_txr_n[i] : 0, 2); lkv("mx", g_txr_mx[i], 3); }
    lput("WIFI.rxr all="); for (int i = 0; i < 48; i++) if (g_rxh[i]) { lhex((uint32_t)i, 2); lput(":"); lhex(g_rxh[i], 4); lput(" "); }
    lput("data="); for (int i = 0; i < 48; i++) if (g_rxh_data[i]) { lhex((uint32_t)i, 2); lput(":"); lhex(g_rxh_data[i], 4); lput(" "); }
    lput("crc="); lhex(g_crcbad, 4); lput(" "); for (int i = 0; i < 48; i++) if (g_crcbad_rate[i]) { lhex((uint32_t)i, 2); lput(":"); lhex(g_crcbad_rate[i], 4); lput(" "); } lput("\r\n");
    lput("WIFI.txq2");    lkv("bcnrate", g_bcnrate, 2); lkv("bcn", g_bcn_n, 4); lkv("bcnage", g_lbon ? (uint32_t)(tms() - g_lastbcn) : 0, 5); lput("\r\n");
}
static void rtw_lost(const char *why)
{
    if (!g_linked) return;
    g_linked = 0; g_lbon = 0; g_have_tk = g_have_gtk = 0; g_bcq = 0; g_qn = 0; g_wst = 1;
    lput("WIFI.link res=down why="); lput(why); lput("\r\n");
    s_logn = 0;                                         /* 5.2.91 진단: 다음 연결의 앞쪽 프레임도 기록 */
}
/* 화면(네트워크 창)의 요청 — 무선 네트워크 찾기 / 연결. 코루틴(live_main)이 처리하고 화면 쪽은 끝날 때까지 코루틴을 돌려 줌 */
static volatile int g_scanreq, g_scandone, g_reconn, g_scanning;
typedef struct { uint8_t ssid[33]; uint8_t sig; uint8_t sec; } ApL;   /* sec: 0 열림 1 암호(WPA2 아님) 2 WPA2 */
static ApL g_apl[64]; static int g_apn;
static int sig_of(const Bss *b)
{
    if (b->pw) { int dbm = (int)b->pw - 110, pc = (dbm + 100) * 100 / 70; return pc < 5 ? 5 : pc > 100 ? 100 : pc; }   /* -30dBm 이상 100%, -100dBm 0% */
    int pc = 20 + (b->cnt > 6 ? 6 : b->cnt) * 8; return pc;                                                         /* PWDB 를 못 읽은 프레임만 들렸을 때 (들린 횟수로 짐작) */
}
static volatile int g_scanabort;
static void rtw_ap_rebuild(void)                       /* 들린 공유기 표 → 화면용 목록 (WPA2 만, 이름 있는 것만, 같은 이름은 센 것 하나, 센 순서) */
{
    g_apn = 0;
    for (int i = 0; i < nbss; i++) {
        const Bss *b = &bss[i];
        if (b->ssidn == 0 || b->ssidn == 0xFF || b->ch >= 14) continue;      /* 이름 있는 2.4GHz 는 모두 보여 준다 (WPA2 가 아니면 표시만) */
        int sg = sig_of(b), at = -1, sc = (b->sec & 2) ? 2 : (b->sec & 1) ? 1 : 0;
        for (int k = 0; k < g_apn; k++) { int same = 1; for (int j = 0; j < b->ssidn + 1; j++) if (g_apl[k].ssid[j] != b->ssid[j]) { same = 0; break; } if (same) { at = k; break; } }
        if (at >= 0) { if (sg > g_apl[at].sig) g_apl[at].sig = (uint8_t)sg; if (sc > g_apl[at].sec) g_apl[at].sec = (uint8_t)sc; continue; }
        if (g_apn >= 64) continue;
        for (int j = 0; j < b->ssidn; j++) g_apl[g_apn].ssid[j] = b->ssid[j]; g_apl[g_apn].ssid[b->ssidn] = 0; g_apl[g_apn].sig = (uint8_t)sg; g_apl[g_apn].sec = (uint8_t)sc; g_apn++;
    }
    for (int i = 1; i < g_apn; i++) { ApL t = g_apl[i]; int j = i - 1; while (j >= 0 && g_apl[j].sig < t.sig) { g_apl[j + 1] = g_apl[j]; j--; } g_apl[j + 1] = t; }
}
/* 약 20초 동안 1~13번 채널을 여러 바퀴 돌며 (채널마다 probe request 를 보내고 비콘·응답을 들음) 목록을 계속 채운다. 늦게 뜨거나 약한 공유기도 잡힘 */
#define SCAN_PASSES 7
static void rtw_do_scan(volatile uint8_t *m, struct rtw_dev *d)
{
    g_scanning = 1; nbss = 0; g_apn = 0; g_scanabort = 0;
    uint8_t fr[40]; for (int i = 0; i < 40; i++) fr[i] = 0;
    fr[0] = 0x40; for (int j = 0; j < 6; j++) { fr[4 + j] = 0xFF; fr[10 + j] = g_mac[j]; fr[16 + j] = 0xFF; }
    fr[24] = 0; fr[25] = 0; fr[26] = 1; fr[27] = 4; fr[28] = 0x82; fr[29] = 0x84; fr[30] = 0x8B; fr[31] = 0x96;
    for (int pass = 0; pass < SCAN_PASSES && !g_scanabort; pass++) {
        for (uint8_t c = 1; c <= 13 && !g_scanabort; c++) {
            rtw8822c_set_channel(d, c, RTW_CHANNEL_WIDTH_20, 0);
            rtw_txpwr2g(m, c);
            uint32_t rx = 0, bc = 0, c2 = 0, bad = 0, hw = 0;
            tx_mgmt(m, fr, 32, 0);
            pumpx(m, 110, c, &rx, &bc, &c2, &bad, &hw);
            tx_mgmt(m, fr, 32, 0);
            pumpx(m, 110, c, &rx, &bc, &c2, &bad, &hw);
            rtw_ap_rebuild();
        }
    }
    rtw8822c_set_channel(d, g_chan ? g_chan : 6, RTW_CHANNEL_WIDTH_20, 0); rtw_txpwr2g(m, g_chan ? g_chan : 6);
    rtw_ap_rebuild();
    g_lastbcn = tms();
    lput("WIFI.ui scan n="); lhex((uint32_t)g_apn, 2); lkv("passes", SCAN_PASSES, 1); lput("\r\n");
    for (int i = 0; i < g_apn; i++) { lput("WIFI.ui ap ssid="); lput((const char *)g_apl[i].ssid); lkv("sig", g_apl[i].sig, 2); lput("\r\n"); }
    nbss = 0; g_scanning = 0;
}
/* 코루틴 본체: 칩 초기화 → 첫 접속 → (감시 | 재접속) 되풀이 */
static void live_main(void)
{
    volatile uint8_t *m = g_m8;
    rtwdet_work();
    if (!g_ready) { lput("WIFI.live res=fail why=chip_not_ready\r\n"); return; }
    g_quiet = 1;
    for (;;) {
        if (g_scanreq) { rtw_do_scan(m, &g_rd); g_scandone = 1; g_scanreq = 0; }
        if (g_reconn) { g_reconn = 0; if (g_linked) rtw_lost("user_request"); g_relink = 0; g_wst = 1; }
        if (g_linked) {
            pump(m, 1);
            while (qpop(0)) {
                if (g_cur.len >= 26 && !__builtin_memcmp(g_cur.d + 10, g_bssid, 6) && (g_cur.fc0 == 0xC0 || g_cur.fc0 == 0xA0)) rtw_lost(g_cur.fc0 == 0xC0 ? "deauth" : "disassoc");
            }
            if (g_linked) rx_stat();                    /* 5.2.91 진단: 10초마다 받기 요약 */
            if (g_linked && (int32_t)(tms() - g_lastbcn) > 10000) rtw_lost("no_beacon");
            if (g_linked && g_txfail >= 20) rtw_lost("tx_stuck");
        } else {
            if (!g_ssid[0]) { g_wst = 0; pump(m, 200); continue; }      /* 공유기를 아직 안 골랐다 (화면에서 고르면 g_reconn) */
            int wait = g_relink == 0 ? 300 : g_relink < 5 ? 3000 : 15000;
            for (int w = 0; w < wait && !g_reconn && !g_scanreq; w += 100) pump(m, 100);
            if (g_reconn || g_scanreq) continue;
            g_relink++; g_txfail = 0;
            int ok = rtw_live_connect(m, &g_rd);
            if (g_relink < 20 || ok) { lput("WIFI.relink try="); lhex(g_relink, 4); lput(ok ? " res=ok" : " res=fail"); lput("\r\n"); }
            if (ok) g_relink = 0;
        }
    }
}
/* 화면에서 부르는 함수 (메인 스택) */
int rtwdet_ui_scan_begin(void)                         /* 찾기 시작 (끝나기를 기다리지 않음. 프레임마다 get 으로 목록을 읽음) */
{
    if (!g_live || g_co_done) return -1;
    if (g_scanreq || g_scanning) return 0;
    g_scandone = 0; g_scanreq = 1; g_scanabort = 0;
    return 0;
}
int rtwdet_ui_scanning(void) { return g_scanreq || g_scanning; }
void rtwdet_ui_scan_stop(void) { if (g_scanreq && !g_scanning) g_scanreq = 0; g_scanabort = 1; }
int rtwdet_ui_scan_get(char ssid[][36], int *sig, int *sec, int max)
{
    int n = g_apn > max ? max : g_apn;
    for (int i = 0; i < n; i++) { int j = 0; for (; g_apl[i].ssid[j] && j < 35; j++) ssid[i][j] = (char)g_apl[i].ssid[j]; ssid[i][j] = 0; sig[i] = g_apl[i].sig; sec[i] = g_apl[i].sec; }
    return n;
}
int rtwdet_ui_connect(const char *ssid, const char *pass)
{
    if (!g_live || g_co_done || !ssid) return -1;
    rtwdet_setcred(ssid, pass);
    lput(ssid[0] ? "WIFI.ui connect ssid=" : "WIFI.ui forget"); lput(ssid); lput("\r\n");        /* 비밀번호는 기록하지 않는다 */
    g_reconn = 1;
    return 0;
}
int rtwdet_ui_state(void) { return g_reconn ? 1 : g_linked ? 2 : g_wst; }   /* 0 미지정 1 연결 중 2 연결됨 3 공유기 못 찾음 4 접속 실패(암호?) */
void rtwdet_live_enable(uint32_t (*clk)(void)) { g_clk = clk; g_live = 1; g_linked = 0; g_ready = 0; co_init(live_main); g_pre = 1; }
static int nic_probe(void) { return g_pre; }
static void nic_mac(uint8_t o[6]) { for (int i = 0; i < 6; i++) o[i] = g_mac[i]; }
static int nic_link(void) { return g_linked; }
static int nic_step(void) { co_resume(); if (g_linked) return 1; if (g_co_done) return -1; return 0; }
static int nic_poll(void) { co_resume(); return 0; }
static int nic_tx(const uint8_t *f, int n)
{
    static uint8_t pl[700];
    if (g_scanning || !g_linked || !g_have_tk || n < 14 || n - 14 + 8 > 640) return -1;
    pl[0] = 0xAA; pl[1] = 0xAA; pl[2] = 3; pl[3] = pl[4] = pl[5] = 0; pl[6] = f[12]; pl[7] = f[13];
    for (int i = 14; i < n; i++) pl[8 + i - 14] = f[i];
    g_txrate = 4; g_dr_last = 4; g_drate = 4;   /* 모든 데이터 프레임을 OFDM 6M(모든 AP 가 지원하는 기본 속도)로 고정 — 속도 돌리기 실험 제거 */
    int ok = tx_data(g_m8, f, pl, n - 14 + 8, 1);
    if (ok) g_txfail = 0; else g_txfail++;
    if (f[12] == 8 && f[13] == 0) { int t = dhcp_type(f + 14, n - 14); if (t) { s_dhtx++; if (t == 3) dhcp_optlog("tx3", f + 14, n - 14); if (s_txlog < 30) { s_txlog++; lput("WIFI.dhcp tx"); lkv("type", (uint32_t)t, 1); lkv("n", (uint32_t)n, 3); lkv("ok", (uint32_t)(ok != 0), 1); lkv("rate", g_txrate, 1); lkv("sn", g_txsn, 3); if (n >= 14 + 28 + 8) lkv("xid", (uint32_t)(f[14 + 32] << 24 | f[14 + 33] << 16 | f[14 + 34] << 8 | f[14 + 35]), 8); lput("\r\n"); } } }   /* 5.2.91 진단 (5.2.94: 순번·xid) */
    return ok ? 0 : -1;
}
static int nic_rx(uint8_t *f, int max)
{
    if (!g_linked) return 0;
    uint32_t a = 0, b = 0, c = 0, e = 0, h = 0; rx_drain(g_m8, g_chan, &a, &b, &c, &e, &h);
    while (qpop(1)) {
        g_dec = 0;
        int n = rxdata();
        s_rxd++; s_dec[g_dec & 7]++;                                                    /* 5.2.91 진단 */
        if (!g_dec) { s_bss++;                                                          /* 다른 공유기(BSSID)·방향이 다른 프레임 */
            static int s_rejlog;                                                        /* 5.2.104 진단: 버린 프레임이 무엇인지 (처음 40개) */
            if (s_rejlog < 40) { s_rejlog++; const uint8_t *q = g_cur.d; lput("WIFI.rej why="); lhex(g_rej, 1); lkv("fc", (uint32_t)(q[0] << 8 | q[1]), 4); lkv("n", g_cur.len, 3);
                lput(" a1="); for (int j = 0; j < 6; j++) lhex(q[4 + j], 2); lput(" a2="); for (int j = 0; j < 6; j++) lhex(q[10 + j], 2); lput(" a3="); for (int j = 0; j < 6; j++) lhex(q[16 + j], 2);
                if (g_cur.len >= 30) { lput(" b="); for (int j = 24; j < 32 && j < (int)g_cur.len; j++) lhex(q[j], 2); }
                lput("\r\n"); }
            continue; }
        rx_log(g_cur.d[4] & 1, n);
        if (n >= 8 + 28 && g_pl[6] == 8 && g_pl[7] == 0) { int t = dhcp_type(g_pl + 8, n - 8); if (t) { s_dhrx++; if (t == 2 || t == 5) { g_drate = g_dr_last; dhcp_optlog(t == 2 ? "rx2" : "rx5", g_pl + 8, n - 8); } lput("WIFI.dhcp rx"); lkv("rate", g_drate, 1); lkv("type", (uint32_t)t, 1); lkv("mc", (uint32_t)(g_cur.d[4] & 1), 1); lkv("dec", g_dec, 1); lkv("n", (uint32_t)n, 3); lkv("xid", (uint32_t)(g_pl[8 + 32] << 24 | g_pl[8 + 33] << 16 | g_pl[8 + 34] << 8 | g_pl[8 + 35]), 8); lkv("yi", (uint32_t)(g_pl[8 + 44] << 24 | g_pl[8 + 45] << 16 | g_pl[8 + 46] << 8 | g_pl[8 + 47]), 8); lput("\r\n"); } }
        if (n < 8 + 28 || g_pl[0] != 0xAA || g_pl[1] != 0xAA || g_pl[2] != 3) { s_llc++; continue; }
        if (g_pl[6] == 0x88 && g_pl[7] == 0x8E) { live_eapol(g_m8, n); continue; }     /* 연결 뒤 EAPOL (M3 다시 옴·그룹 키 교체) 은 표준대로 답함 */
        if (g_pl[6] != 8 || (g_pl[7] != 0 && g_pl[7] != 6)) { s_et++; continue; }       /* IPv4·ARP 만 (EAPOL·IPv6 등은 버림) */
        if (!__builtin_memcmp(g_cur.d + 16, g_mac, 6)) { s_self++; continue; }          /* 내가 보낸 방송이 AP 에서 되돌아온 것 */
        s_pass++;
        int pl = n - 8; if (pl + 14 > max) continue;
        for (int i = 0; i < 6; i++) { f[i] = g_cur.d[4 + i]; f[6 + i] = g_cur.d[16 + i]; }
        f[12] = g_pl[6]; f[13] = g_pl[7];
        for (int i = 0; i < pl; i++) f[14 + i] = g_pl[8 + i];
        return pl + 14;
    }
    return 0;
}
const Nic nic_rtw = { .name = "RTL8822CE", .probe = nic_probe, .mac = nic_mac, .link = nic_link, .tx = nic_tx, .rx = nic_rx, .step = nic_step, .poll = nic_poll, .mtu = 600 };
