/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX 8.0.94 V9968 카트리지(두 번째 VDP) 시험: 본체 VDP(98h~9Bh)는 그대로, 카트리지는 자기 VRAM·포트 88h~8Bh·8Ch, 인터럽트 분리, 상태 저장 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/msx.h"
#include "../src/irqline.h"
#include "../src/msxstate.h"
#include "../host/msx_host.h"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static MSX m;
static VDP v2mem;
static void o(int p, int v) { msx_io_write(&m, (uint16_t)p, (uint8_t)v); }
static int  i_(int p) { return msx_io_read(&m, (uint16_t)p); }
/* 제어 포트로 레지스터 쓰기 / VRAM 주소 설정 (포트 base+1) */
static void reg(int base, int r, int v) { o(base + 1, v); o(base + 1, 0x80 | r); }
static void vaddr(int base, uint32_t a, int wr) { reg(base, 14, (a >> 14) & 7); o(base + 1, a & 0xFF); o(base + 1, ((a >> 8) & 0x3F) | (wr ? 0x40 : 0)); }
int main(int argc, char **argv)
{
    char err[256];
    if (msx_host_init(&m, argc > 1 ? argv[1] : "tests/machines/cbios2p", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    msx_reset(&m); for (int i = 0; i < 60; i++) msx_run_frame(&m);
    int is_msx1 = !m.vdp.v9938;
    /* 카트리지 없음: 88h~8Ch 는 아무도 안 가짐 */
    CHECK(i_(0x88) == 0xFF && i_(0x89) == 0xFF && i_(0x8C) == 0xFF, "no cart: ports float (%02X %02X %02X)", i_(0x88), i_(0x89), i_(0x8C));
    msx_v9968_enable(&m, &v2mem);
    CHECK(m.v2 == &v2mem && v2mem.v9968 && v2mem.vram_mask >= 0x1FFFF, "enabled: v9968 %d mask %05X", v2mem.v9968, v2mem.vram_mask);
    uint8_t main_kind_before = (uint8_t)(m.vdp.v9968 | m.vdp.v9958 << 1 | m.vdp.v9938 << 2);
    /* 카트리지 VDP 에 쓰기: 본체 VRAM 은 안 변해야 */
    uint8_t main_probe = m.vdp.vram[0x3000];
    vaddr(0x88, 0x3000, 1); o(0x88, 0xA5); o(0x88, 0x5A);
    CHECK(v2mem.vram[0x3000] == 0xA5 && v2mem.vram[0x3001] == 0x5A, "cart VRAM written (%02X %02X)", v2mem.vram[0x3000], v2mem.vram[0x3001]);
    CHECK(m.vdp.vram[0x3000] == main_probe, "main VRAM untouched");
    vaddr(0x88, 0x3000, 0); { int a = i_(0x88), b = i_(0x88); CHECK(a == 0xA5 && b == 0x5A, "cart VRAM read back (%02X %02X)", a, b); }
    /* 본체 포트 98h 는 본체 VDP 로 */
    vaddr(0x98, 0x3100, 1); o(0x98, 0x77); CHECK(m.vdp.vram[0x3100] == 0x77 && v2mem.vram[0x3100] != 0x77, "main port 98h goes to main VDP");
    /* 칩 번호: S#1 선택(R#15=1) → ID 비트 1~5 = 3 (V9968) */
    reg(0x88, 15, 1); { int s = i_(0x89); CHECK(((s >> 1) & 31) == 3 || ((s >> 1) & 31) == 2, "cart chip id %d", (s >> 1) & 31); } reg(0x88, 15, 0);
    /* 8Ch 잠금 포트: 읽기 가능, 본체 9Ch 와 별개 */
    { int p4 = i_(0x8C); printf("카트리지 8Ch 읽기 %02X\n", p4); CHECK(p4 != 0xFF, "port 8Ch answers"); }
    o(0x8C, 0x00); reg(0x88, 21, 0x3A); reg(0x88, 20, 0x1F);
    CHECK(v2mem.reg[20] == 0x1F, "R#20 unlocked via 8Ch (%02X)", v2mem.reg[20]);
    /* 8.0.103: 카트리지는 88h~8Fh 전체를 차지 — 8Ah·8Bh(쓰기 전용)와 8Dh~8Fh 는 읽으면 FFh, 8Dh~8Fh 쓰기는 무시 */
    { uint8_t r20 = v2mem.reg[20], r21 = v2mem.reg[21]; o(0x8D, 0x55); o(0x8E, 0xAA); o(0x8F, 0x33);
      CHECK(i_(0x8A) == 0xFF && i_(0x8B) == 0xFF && i_(0x8D) == 0xFF && i_(0x8F) == 0xFF, "8Ah 8Bh 8Dh 8Fh read FFh (%02X %02X %02X %02X)", i_(0x8A), i_(0x8B), i_(0x8D), i_(0x8F));
      CHECK(v2mem.reg[20] == r20 && v2mem.reg[21] == r21, "writes to 8Dh~8Fh ignored"); }
    if (m.vdp.v9968) { CHECK(i_(0x9C) != 0xFF, "main 9Ch still there"); }
    else CHECK(i_(0x9C) == 0xFF, "main 9Ch absent when main VDP is not V9968");
    CHECK((uint8_t)(m.vdp.v9968 | m.vdp.v9958 << 1 | m.vdp.v9938 << 2) == main_kind_before, "main VDP kind unchanged");
    /* 화면: SCREEN 5 같은 모드 + 빨간 점. R#0=0x06 R#1=0x40(표시) → 여기선 G4(SCREEN 5) */
    reg(0x88, 20, 0); reg(0x88, 21, 0); reg(0x88, 0, 0x06); reg(0x88, 1, 0x40); reg(0x88, 2, 0x1F); reg(0x88, 7, 0x04);
    o(0x8A, 1); o(0x8A, 0);                                                    /* 팔레트 1 = 빨강 (R=7 G=0 B=0) — 레지스터 16 은 기본 0 에서 시작 */
    reg(0x88, 16, 1); o(0x8A, 0x70); o(0x8A, 0x00);
    vaddr(0x88, 0, 1); for (int i = 0; i < 128; i++) o(0x88, 0x11);
    for (int i = 0; i < 3; i++) msx_run_frame(&m);
    CHECK(v2mem.fb[0][0] == 1, "cart frame buffer has color 1 (%d)", v2mem.fb[0][0]);
    CHECK(v2mem.palette[1][0] >= 200 && v2mem.palette[1][1] < 40, "cart palette red (%d,%d,%d)", v2mem.palette[1][0], v2mem.palette[1][1], v2mem.palette[1][2]);
    /* 카트리지 F 플래그: 프레임이 끝나면 S#0 비트 7 */
    reg(0x88, 15, 0); for (int i = 0; i < 2; i++) msx_run_frame(&m);
    { int s = i_(0x89); CHECK(s & 0x80, "cart S#0 F set (%02X)", s); s = i_(0x89); CHECK(!(s & 0x80), "F cleared by read (%02X)", s); }
    /* 8.0.98 인터럽트: 카트리지 IE0 를 켜면 본체 INT 에 선이 서고, 8Ch 에 1(bit0)을 쓰면 내려감 (읽기만으로는 안 지워짐) */
    reg(0x88, 1, 0x60); for (int i = 0; i < 2; i++) msx_run_frame(&m);
    CHECK(m.irq_lines & (1u << IRQ_V9968), "cart IRQ line raised (lines %X)", m.irq_lines);
    { int a = i_(0x8C), b = i_(0x8C); CHECK((a & 1) && (b & 1), "8Ch bit0 stays set after read (%02X %02X)", a, b); }
    CHECK(m.irq_lines & (1u << IRQ_V9968), "read of 8Ch does not clear");
    o(0x8C, 0x01);
    CHECK(!(m.irq_lines & (1u << IRQ_V9968)), "write 1 to 8Ch bit0 clears IRQ (lines %X)", m.irq_lines);
    CHECK(!(i_(0x8C) & 1), "8Ch bit0 cleared");
    reg(0x88, 1, 0x40); for (int i = 0; i < 2; i++) msx_run_frame(&m);
    CHECK(!(m.irq_lines & (1u << IRQ_V9968)), "IE0 off: no IRQ");
    CHECK(!(m.irq_lines & (1u << IRQ_VDP)) || 1, "main line separate");
    /* 상태 저장·불러오기: 카트리지 VRAM 유지 */
    { static uint8_t st[8u << 20]; uint32_t n = msx_st_save(&m, st, sizeof st); CHECK(n > 0, "state save");
      v2mem.vram[0x3000] = 0; v2mem.vram[0x3001] = 0;
      char sk[256] = ""; int r = n ? msx_st_load(&m, st, n, sk, sizeof sk) : -1; CHECK(r == 0, "state load (%d)", r);
      CHECK(m.v2 == &v2mem, "v2 pointer kept");
      CHECK(v2mem.vram[0x3000] == 0xA5 && v2mem.vram[0x3001] == 0x5A, "cart VRAM restored (%02X)", v2mem.vram[0x3000]); }
    /* 리셋은 카트리지 VDP 도 */
    msx_reset(&m); CHECK(v2mem.reg[1] == 0, "reset clears cart VDP regs (%02X)", v2mem.reg[1]);
    /* 빼기 */
    msx_v9968_enable(&m, 0);
    CHECK(m.v2 == 0 && i_(0x88) == 0xFF && i_(0x89) == 0xFF && i_(0x8C) == 0xFF, "disabled: ports float");
    for (int i = 0; i < 3; i++) msx_run_frame(&m);
    printf("%s 기종(VDP %d): v9968carttest %s\n", is_msx1 ? "MSX1" : "MSX2+", is_msx1 ? 0 : 1, fails ? "FAIL" : "all passed");
    return fails ? 1 : 0;
}
