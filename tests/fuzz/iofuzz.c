/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
#include "msx_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t h = 1469598103934665603ull;
static void hb(const void *p, size_t n) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } }
static uint32_t rs = 12345; static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
int main(int argc, char **argv)
{
    static MSX m; static V9990 v9; static uint8_t wm[4u << 20]; char err[256];
    if (msx_host_init(&m, argv[1], err, sizeof err)) { printf("init %s\n", err); return 1; }
    opl4_set_memory(wm, 2u << 20); msx_moonsound_enable(&m, 1); msx_audio_enable(&m, 1); msx_v9990_enable(&m, &v9);
    if (argc > 2) rs = (uint32_t)atoi(argv[2]);
    for (int f = 0; f < 120; f++) msx_run_frame(&m);
    static const uint8_t hot[] = { 0x10,0x11,0x12,0x28,0x29,0x2A,0x2B,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x3A,0x40,0x41,0x42,0x43,0x48,0x60,0x61,0x64,0x67,0x6F,0x77,0x7C,0x7D,0x7E,0x7F,0x8E,0x90,0x91,
        0x98,0x99,0x9A,0x9B,0xA0,0xA1,0xA2,0xA4,0xA5,0xA7,0xA8,0xA9,0xAA,0xAB,0xB4,0xB5,0xC0,0xC1,0xC4,0xC5,0xC6,0xC7,0xD0,0xD4,0xD8,0xD9,0xDA,0xDB,
        0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEC,0xED,0xEE,0xEF,0xF4,0xFC,0xFD,0xFE,0xFF };
    for (int i = 0; i < 300000; i++) {
        {   /* I/O 가 딸린 카트를 차례로 꽂고 빼며 (MegaRAM 8Eh, Zemina90 77h, Beer IDE 30h~, GIDE 6xh, Gouda 34h~, 네트워크) */
            static const int at[] = { 40000, 70000, 130000, 160000, 230000, 260000 }; static const int mp[] = { MAPPER_MEGARAM, MAPPER_ZEMINA90, MAPPER_BEERIDE, MAPPER_GIDE, MAPPER_GOUDA, MAPPER_NET };
            static uint8_t crom[256u << 10];
            for (int q = 0; q < 6; q++) if (i == at[q]) {
                for (unsigned k = 0; k < sizeof crom; k++) crom[k] = (uint8_t)(k * 7 + q);
                msx_eject_cart(&m, 0);
                { uint8_t v = msx_io_read(&m, 0x98); hb(&v, 1); }   /* 사슬을 빼고 난 상태로 한 번 만들게 */
                msx_insert_cart(&m, 0, crom, mp[q] == MAPPER_GIDE ? 16384 : mp[q] == MAPPER_NET ? 16384 : (uint32_t)sizeof crom, (MapperType)mp[q]);
                { uint8_t v = msx_io_read(&m, 0x98); hb(&v, 1); }   /* 꽂은 직후 사슬 (IDE·네트워크 깃발은 아직 0) */
                msx_run_frame(&m);                      /* 프레임 시작에 갱신되는 깃발 → 사슬 다시 */
            }
            if (i == 290000) msx_eject_cart(&m, 0);
        }
        if (i == 100000) msx_v9990_enable(&m, 0);           /* 장치를 빼고 꽂아도 사슬이 맞는지 */
        if (i == 200000) msx_v9990_enable(&m, &v9);
        uint32_t r = rnd();
        int port = (r & 3) ? hot[(r >> 8) % sizeof hot] : (int)((r >> 8) & 0xFF);
        if (port == 0xA8 || port == 0xFC || port == 0xFD || port == 0xFE || port == 0xFF || port == 0xE5 || port == 0xA7) { if (r & 0x10000) continue; }   /* 슬롯·CPU 바꾸기는 드물게 */
        if (r & 0x80000) { uint8_t v = (uint8_t)(rnd()); msx_io_write(&m, (uint16_t)port, v); }
        else { uint8_t v = msx_io_read(&m, (uint16_t)port); hb(&v, 1); }
        m.cpu.cycles += 11;
        if ((i % 5000) == 4999) {
            hb(m.vdp.reg, sizeof m.vdp.reg); hb(m.vdp.vram, sizeof m.vdp.vram); hb(m.psg.reg, 16); hb(m.mapper_reg, 4); hb(&m.primary, 1);
            hb(m.opl4.fmreg, sizeof m.opl4.fmreg); hb(v9.regs, sizeof v9.regs); hb(&m.irq_lines, 4); hb(m.cart[0].bank, 4); hb(&m.cart[0].mram_we, 1); hb(&m.cart[0].present, sizeof m.cart[0].present);
            printf("%d %016llx\n", i, (unsigned long long)h);
        }
    }
    printf("final %016llx\n", (unsigned long long)h);
    return 0;
}
