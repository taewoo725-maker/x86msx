/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* amdedid.c - DDC2 비트뱅 EDID (설명 amdedid.h). 접근 범위: BAR5 + (0x34C0+0x28d0+4)*4 .. +4 레지스터 */
#include "amdedid.h"

#define DDC_N 2                                   /* 2b 시험 대상: Y=3 이던 DDC2 하나 */
#define REG(mm, idx) ((volatile uint32_t *)(mm))[0x34C0u + (uint32_t)(idx)]
#define DDC_BASE(n) (0x28d0u + (uint32_t)((n) - 1) * 4u)   /* +0 MASK +1 A +2 EN +3 Y */

static inline void p_outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }
static void udelay(uint32_t us) { for (uint32_t i = 0; i < us * 4; i++) p_outb(0x80, 0); }   /* 포트 0x80 쓰기 ≈ 0.3~1µs, 4배 느리게 */

static uint32_t rd(volatile uint32_t *mm, uint32_t off) { return REG(mm, DDC_BASE(DDC_N) + off); }
static void wr(volatile uint32_t *mm, uint32_t off, uint32_t v) { REG(mm, DDC_BASE(DDC_N) + off) = v; }
static void pin_lo(volatile uint32_t *mm, int scl) {
    uint32_t sh = scl ? 0 : 8;
    wr(mm, 1, rd(mm, 1) & ~(1u << sh)); wr(mm, 2, rd(mm, 2) | (1u << sh)); }
static void pin_hi(volatile uint32_t *mm, int scl) { uint32_t sh = scl ? 0 : 8; wr(mm, 2, rd(mm, 2) & ~(1u << sh)); }
static int pin_rd(volatile uint32_t *mm, int scl) { return (int)((rd(mm, 3) >> (scl ? 0 : 8)) & 1); }
static int scl_wait_hi(volatile uint32_t *mm) { for (int i = 0; i < 2000; i++) { if (pin_rd(mm, 1)) return 0; udelay(1); } return -1; }
static void bus_recover(volatile uint32_t *mm) {
    pin_hi(mm, 0);
    for (int i = 0; i < 9; i++) { pin_lo(mm, 1); udelay(5); pin_hi(mm, 1); udelay(5); } }
static void i2c_start(volatile uint32_t *mm) {        /* 유휴(SCL·SDA 모두 HIGH)에서의 시작: 4.0.34 초판에서 응답(wr:2222)이 있던 방식 */
    pin_hi(mm, 0); pin_hi(mm, 1); udelay(5); pin_lo(mm, 0); udelay(5); pin_lo(mm, 1); udelay(5); }
static void i2c_rstart(volatile uint32_t *mm) {       /* 반복 시작: SCL을 먼저 내리고 SDA를 놓은 뒤 SCL 올리고 SDA 내림 */
    pin_lo(mm, 1); udelay(5); pin_hi(mm, 0); udelay(5); pin_hi(mm, 1); udelay(5); pin_lo(mm, 0); udelay(5); pin_lo(mm, 1); udelay(5); }
static void i2c_stop(volatile uint32_t *mm) { pin_lo(mm, 0); pin_lo(mm, 1); udelay(5); pin_hi(mm, 1); udelay(5); pin_hi(mm, 0); udelay(5); }
static int wr_byte(volatile uint32_t *mm, uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        pin_lo(mm, 1);
        if ((b >> i) & 1) pin_hi(mm, 0); else pin_lo(mm, 0);
        udelay(5); pin_hi(mm, 1); udelay(5);
        if (scl_wait_hi(mm)) return -1;
        udelay(5); }
    pin_lo(mm, 1); pin_hi(mm, 0); udelay(5); pin_hi(mm, 1); udelay(5);
    int ack = !pin_rd(mm, 0);
    if (scl_wait_hi(mm)) return -1;
    udelay(5); return ack ? 0 : -2; }
static int rd_byte(volatile uint32_t *mm, int ack, uint8_t *o) {
    uint8_t b = 0;
    pin_lo(mm, 1); udelay(5); pin_hi(mm, 0);                /* SCL 낮춘 뒤 SDA 놓기 (SCL HIGH 중 놓으면 정지 신호가 됨) */
    for (int i = 7; i >= 0; i--) {
        pin_lo(mm, 1); udelay(5); pin_hi(mm, 1); udelay(5);
        if (scl_wait_hi(mm)) return -1;
        if (pin_rd(mm, 0)) b |= (uint8_t)(1u << i);
        udelay(5); }
    pin_lo(mm, 1);
    if (ack) pin_lo(mm, 0); else pin_hi(mm, 0);
    udelay(5); pin_hi(mm, 1); udelay(5);
    if (scl_wait_hi(mm)) return -1;
    udelay(5); *o = b; return 0; }

static int stg, rise_n, pw[4], b1ok;
static uint8_t b1;
static void run_tests(volatile uint32_t *mm)
{
    {   /* 판별 시험 (다른 AI 답 A1): ① SDA 상승 속도 ② 쓰기 전용 A0+00 ×4 ③ 1바이트 읽기 (offset 0) */
        pin_lo(mm, 0); udelay(20); pin_hi(mm, 0);
        int cnt = 0; while (!pin_rd(mm, 0) && cnt < 20000) cnt++;
        rise_n = cnt;                                                   /* MMIO 읽기 횟수 (≈0.5~1µs/회), 20000이면 안 오름 */
        for (int q = 0; q < 4; q++) {
            i2c_start(mm); int a = wr_byte(mm, 0xA0); int b = a == 0 ? wr_byte(mm, 0x00) : -9; i2c_stop(mm);
            pw[q] = a == 0 ? (b == 0 ? 2 : 1) : 0; udelay(100);
        }
        b1ok = 0;
        i2c_start(mm);
        if (!wr_byte(mm, 0xA0) && !wr_byte(mm, 0x00)) { i2c_rstart(mm); if (!wr_byte(mm, 0xA1) && !rd_byte(mm, 0, &b1)) b1ok = 1; }
        i2c_stop(mm); udelay(100);
    }
}

/* 0 OK, -1 풀업 없음/버스 busy, -2 NACK, -3 헤더, -4 체크섬, -5 시간초과 */
static int edid_read(volatile uint32_t *mm, uint8_t out[128], uint32_t *y_after, int var)
{
    uint32_t sv0 = rd(mm, 0), sv1 = rd(mm, 1), sv2 = rd(mm, 2);
    int r = 0;
    wr(mm, 0, (sv0 | 0x0101u) & ~0x1010u);                 /* SCL·SDA를 GPIO(소프트웨어) 소유로, 풀다운 끔 */
    bus_recover(mm);
    *y_after = rd(mm, 3);
    if (!pin_rd(mm, 1) || !pin_rd(mm, 0)) { r = -1; goto done; }
    stg = 1; i2c_start(mm);
    r = wr_byte(mm, 0xA0);
    if (!r) { stg = 2; r = wr_byte(mm, 0x00); }
    if (r) { i2c_stop(mm); r = r == -1 ? -5 : -2; goto done; }
    if (var) { i2c_stop(mm); udelay(50); }                  /* 변형 1: 정지 후 새 시작 (반복 시작 안 씀) */
    stg = 3; if (var) i2c_start(mm); else i2c_rstart(mm);
    r = wr_byte(mm, 0xA1);
    if (r) { i2c_stop(mm); r = r == -1 ? -5 : -2; goto done; }
    stg = 4;
    for (int i = 0; i < 128; i++)
        if (rd_byte(mm, i < 127, &out[i])) { i2c_stop(mm); r = -5; goto done; }
    i2c_stop(mm);
    {
        static const uint8_t hd[8] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
        for (int i = 0; i < 8; i++) if (out[i] != hd[i]) { r = -3; goto done; }
        uint8_t s = 0; for (int i = 0; i < 128; i++) s = (uint8_t)(s + out[i]);
        if (s) r = -4;
    }
done:
    bus_recover(mm); udelay(200); run_tests(mm);            /* 진단 시험은 본 읽기 뒤에 (먼저 하면 장치 상태가 흐트러짐) */
    wr(mm, 2, sv2); wr(mm, 1, sv1); wr(mm, 0, sv0);       /* 복원 (EN → A → MASK 순서) */
    return r;
}

static void hx(char *o, uint32_t v, int digits)
{
    static const char H[] = "0123456789ABCDEF";
    for (int i = 0; i < digits; i++) o[i] = H[(v >> ((digits - 1 - i) * 4)) & 15];
}
static int put(char *l, const char *t) { int n = 0; while (*t) l[n++] = *t++; l[n] = 0; return n; }

static char out[7][64];
static int nout, last_sense = -1, last_r = -9, tries;
static uint64_t t_last;
static inline uint64_t tsc(void) { uint32_t lo, hi; __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
static uint8_t raw_e[128];

int amdedid_info(volatile uint32_t *mm, int sense, char (*lines)[64], int max)
{
    int again = sense && last_r != 0 && tries < 10 && last_sense == sense && tsc() - t_last > 4000000000ull;   /* 실패 시 약 1.5초 간격 자동 재시도 (모니터 대기 해제 대비) */
    if (sense != last_sense || again) {
        if (sense != last_sense) tries = 0;
        tries++; t_last = tsc();
        last_sense = sense; nout = 0;
        uint8_t e[128]; uint32_t y = 0;
        for (int i = 0; i < 128; i++) e[i] = 0;
        int r = -9, tr[2], ts[2];
        for (int v = 0; v < 2 && r != 0; v++) { r = edid_read(mm, e, &y, v); tr[v] = r; ts[v] = stg; udelay(2000); }
        if (r == 0) { tr[1] = 0; ts[1] = 0; }
        last_r = r;
        for (int i = 0; i < 128; i++) raw_e[i] = e[i];
        char *l = out[nout++]; int n = put(l, "amd edid d2: rc=");
        l[n++] = r < 0 ? '-' : '0'; l[n++] = (char)('0' + (r < 0 ? -r : r));
        n += put(l + n, " y="); hx(l + n, y & 0x101u, 3); n += 3;
        n += put(l + n, " cable="); l[n++] = sense ? '1' : '0'; n += put(l + n, " n="); if (tries >= 10) l[n++] = '1'; l[n++] = (char)('0' + (tries >= 10 ? 0 : tries)); l[n] = 0;
        {
            l = out[nout++]; n = put(l, "amd edid try v0:");
            l[n++] = tr[0] < 0 ? '-' : '0'; l[n++] = (char)('0' + (tr[0] < 0 ? -tr[0] : tr[0])); l[n++] = '/'; l[n++] = (char)('0' + ts[0]);
            n += put(l + n, " v1:");
            l[n++] = tr[1] < 0 ? '-' : '0'; l[n++] = (char)('0' + (tr[1] < 0 ? -tr[1] : tr[1])); l[n++] = '/'; l[n++] = (char)('0' + ts[1]);
            l[n] = 0;
        }
        {
            l = out[nout++]; n = put(l, "amd i2c rise=");
            { uint32_t x = (uint32_t)rise_n; char d[6]; int dn = 0; do { d[dn++] = (char)('0' + x % 10); x /= 10; } while (x && dn < 5); while (dn) l[n++] = d[--dn]; }
            n += put(l + n, " wr:");
            for (int q = 0; q < 4; q++) l[n++] = (char)('0' + pw[q]);
            n += put(l + n, " b0=");
            if (b1ok) { hx(l + n, b1, 2); n += 2; } else n += put(l + n, "--");
            l[n] = 0;
        }
        if (r == -3 || r == -4) {                                      /* 읽은 첫 8바이트 (헤더 00 FF FF FF FF FF FF 00 이어야 함) */
            l = out[nout++]; n = put(l, "amd edid raw:");
            for (int i = 0; i < 8; i++) { l[n++] = ' '; hx(l + n, e[i], 2); n += 2; }
            l[n] = 0;
        }
        if (r == 0) {
            l = out[nout++]; n = put(l, "amd edid mfg=");
            l[n++] = (char)('@' + ((e[8] >> 2) & 31)); l[n++] = (char)('@' + (((e[8] & 3) << 3) | (e[9] >> 5))); l[n++] = (char)('@' + (e[9] & 31));
            n += put(l + n, " prod="); hx(l + n, (uint32_t)e[10] | ((uint32_t)e[11] << 8), 4); n += 4;
            n += put(l + n, " ver="); l[n++] = (char)('0' + (e[18] & 15)); l[n++] = '.'; l[n++] = (char)('0' + (e[19] & 15));
            n += put(l + n, " ext="); l[n++] = (char)('0' + (e[126] % 10)); l[n] = 0;
            if (e[54] | e[55]) {                                      /* 첫 상세 타이밍 = 선호 해상도 */
                uint32_t h = (uint32_t)e[56] | ((uint32_t)(e[58] & 0xF0) << 4), v = (uint32_t)e[59] | ((uint32_t)(e[61] & 0xF0) << 4);
                l = out[nout++]; n = put(l, "amd edid pref ");
                char t[12]; int m = 0; uint32_t x = h; char d[6]; int dn = 0;
                do { d[dn++] = (char)('0' + x % 10); x /= 10; } while (x && dn < 5);
                while (dn) t[m++] = d[--dn]; t[m++] = 'x'; x = v;
                do { d[dn++] = (char)('0' + x % 10); x /= 10; } while (x && dn < 5);
                while (dn) t[m++] = d[--dn]; t[m] = 0;
                put(l + n, t);
            }
        }
    }
    int k = 0;
    for (int i = 0; i < nout && k < max; i++, k++) { int n = 0; while (out[i][n]) { lines[k][n] = out[i][n]; n++; } lines[k][n] = 0; }
    return k;
}
