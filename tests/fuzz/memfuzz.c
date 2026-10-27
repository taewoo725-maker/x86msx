/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 메모리·매퍼 동등성 무작위 시험 (tests/fuzz/fuzzcmp.sh 가 이전 판과 비교) */
#include "msx_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint64_t h;
static void hb(const void *p, size_t n) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } }
static uint32_t rs; static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static uint8_t rom[8u << 20];
int main(int argc, char **argv)
{
    static MSX m; char err[256]; setvbuf(stdout, 0, _IONBF, 0); const char *skip = getenv("SKIP");
    int nmap = MAPPER_COUNT;
    for (int mp = 0; mp < nmap; mp++) {
        for (int sz = 0; sz < 3; sz++) {
            static const uint32_t sizes[3] = { 32768, 131072, 524288 };
            uint32_t size = sizes[sz]; { char k[8]; snprintf(k, sizeof k, ",%d,", mp); if (skip && strstr(skip, k)) continue; }
            fprintf(stderr, "start %d %u\n", mp, size);
            if (msx_host_init(&m, argv[1], err, sizeof err)) { printf("init %s\n", err); return 1; }
            rs = 1234567u + (uint32_t)mp * 77u + (uint32_t)sz; h = 1469598103934665603ull;
            for (uint32_t i = 0; i < size; i++) rom[i] = (uint8_t)rnd();
            rom[0] = 'A'; rom[1] = 'B';
            int e = msx_insert_cart(&m, 0, rom, size, (MapperType)mp);
            if (e) { printf("map %d size %u insert err %d\n", mp, size, e); continue; }
            for (int f = 0; f < 3; f++) msx_run_frame(&m);
            msx_io_write(&m, 0xA8, 0x55);               /* 모든 페이지 = 슬롯 1 */
            for (int i = 0; i < 60000; i++) {
                uint32_t r = rnd();
                uint16_t a;
                switch (r & 7) {
                case 0: a = (uint16_t)(0x6000 + (rnd() & 0x1FFF)); break;
                case 1: a = (uint16_t)(0x4000 + (rnd() & 0x7FFF)); break;
                case 2: { static const uint16_t sp[] = { 0x5FFE, 0x5FFF, 0x7FF4, 0x7FF5, 0x7FF7, 0x7FFE, 0x7FFF, 0xBFFE, 0xBFFF, 0x9000, 0xB000, 0x9800, 0x9888, 0x988F, 0xB8AF, 0x4045, 0x4000, 0x4001, 0x4104, 0x7C00, 0x7E07, 0x5000, 0x6000, 0x7000, 0x8000, 0xA000, 0x7FFC, 0x7FFD, 0x0AAA, 0x0555, 0x4AAA, 0x4555, 0x8AAA }; a = sp[rnd() % (sizeof sp / sizeof sp[0])]; break; }
                case 3: a = (uint16_t)(rnd()); break;
                default: a = (uint16_t)(0x4000 + (rnd() & 0x7FFF)); break;
                }
                if (a >= 0xC000 && (r & 0x100)) a &= 0xBFFF;   /* 페이지 3 은 RAM 이 있어야 (스택 없음 — CPU 안 돌림) */
                if ((r & 0x3F00000) == 0x100000) { msx_io_write(&m, 0xA8, (uint8_t)rnd()); continue; }   /* 슬롯 바꿈: 내장 FDC·펌웨어·DOS2 칸까지 */
                if ((r & 0x3F00000) == 0x200000) { msx_mem_write(&m, 0xFFFF, (uint8_t)rnd()); continue; }
                if ((r & 0x3F00000) == 0x300000) { static const uint16_t fp[] = { 0x7FF8, 0x7FF9, 0x7FFA, 0x7FFB, 0x7FFC, 0x7FFD, 0x7FFE, 0x7FFF, 0x7FB8, 0x7FB9, 0x7FBA, 0x7FBB, 0x7FBC, 0x6000, 0xBFF8, 0x7FF0, 0x4000, 0x4001 }; a = fp[rnd() % (sizeof fp / sizeof fp[0])]; }
                if ((r & 0x3F00000) == 0x400000 || (r & 0x3F00000) == 0x500000) {   /* 플래시 명령 묶음 (AA·55·명령, 29F040 555h/2AAh 또는 S29GL AAAh/555h) */
                    static const uint16_t pages[4] = { 0x4000, 0x6000, 0x8000, 0xA000 };
                    uint16_t pb = pages[rnd() & 3], c1, c2;
                    if (rnd() & 1) { c1 = 0x555; c2 = 0x2AA; } else { c1 = (rnd() & 1) ? 0xAAA : 0xAAB; c2 = (rnd() & 1) ? 0x555 : 0x554; }
                    if ((rnd() & 7) == 0) msx_mem_write(&m, 0x7FFF, (uint8_t)(0x11 | (rnd() & 0x20)));   /* Yamanooto 플래시 쓰기 켬 */
                    uint8_t cmd; switch (rnd() % 9) { case 0: cmd = 0x90; break; case 1: case 2: case 3: cmd = 0xA0; break; case 4: case 5: cmd = 0x80; break; case 6: cmd = 0x98; break; case 7: cmd = 0xF0; break; default: cmd = 0xFF; break; }
                    if (cmd == 0x98) { msx_mem_write(&m, (uint16_t)(pb | 0xAA), 0x98); }
                    else if (cmd == 0xF0 || cmd == 0xFF) msx_mem_write(&m, (uint16_t)(pb | (rnd() & 0x1FFF)), cmd);
                    else {
                        msx_mem_write(&m, (uint16_t)(pb | c1), 0xAA); msx_mem_write(&m, (uint16_t)(pb | c2), 0x55); msx_mem_write(&m, (uint16_t)(pb | c1), cmd);
                        if (cmd == 0xA0) msx_mem_write(&m, (uint16_t)(pages[rnd() & 3] | (rnd() & 0x1FFF)), (uint8_t)rnd());
                        if (cmd == 0x80) {
                            msx_mem_write(&m, (uint16_t)(pb | c1), 0xAA); msx_mem_write(&m, (uint16_t)(pb | c2), 0x55);
                            if ((rnd() & 31) == 0) msx_mem_write(&m, (uint16_t)(pb | c1), 0x10);
                            else msx_mem_write(&m, (uint16_t)(pages[rnd() & 3] | (rnd() & 0x1FFF)), 0x30);
                        }
                    }
                    for (int k = 0; k < 4; k++) { uint16_t q = (uint16_t)(pages[k] + (rnd() & 0x1FFF)); uint8_t v = msx_mem_read(&m, q); hb(&v, 1); }
                    continue;
                }
                if (r & 0x10000) { uint8_t v = (uint8_t)rnd(); msx_mem_write(&m, a, v); }
                else { uint8_t v = msx_mem_read(&m, a); hb(&v, 1); }
                if ((i & 4095) == 4095) { for (int k = 0; k < 4; k++) { uint16_t q = (uint16_t)(k * 0x4000 + (rnd() & 0x3FFF)); uint8_t v = msx_mem_read(&m, q); hb(&v, 1); } }
            }
            hb(m.cart[0].bank, sizeof m.cart[0].bank); hb(m.cart[0].xbank, sizeof m.cart[0].xbank);
            if (m.cart[0].sram) hb(m.cart[0].sram, 8192);
            hb(m.cart[0].data, m.cart[0].size);
            hb(m.rd, 0); for (int b = 0; b < 8; b++) { uint8_t q[16]; for (int k = 0; k < 16; k++) q[k] = msx_mem_read(&m, (uint16_t)(b * 0x2000 + k)); hb(q, 16); } hb(&m.primary, 1); hb(m.subslot, 4); hb(&m.dos2_bank, sizeof m.dos2_bank);
            printf("map %d size %u %016llx\n", mp, size, (unsigned long long)h);
        }
    }
    return 0;
}
