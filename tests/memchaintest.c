/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 5.2.119 메모리 칸 사슬 동등성 시험: MemDev 를 장치 표(MemOps)로 옮기기 전후에 칸 8개의 읽기·쓰기 사슬이 같아야 함.
 * 기종 × 카트·펌웨어·FDC·DOS2·매퍼 레지스터를 바꾼 무작위 상태(고정 시드) 마다 슬롯을 바꾸고 사슬(msx_mem_owners)과 읽기 값을 해시 → tests/data/memchain_expected.txt 와 비교 */
#include "msx_host.h"
#include <stdio.h>
#include <string.h>
static MSX m;
static uint64_t h = 1469598103934665603ull;
static void mix(uint64_t v) { h ^= v; h *= 1099511628211ull; }
static uint8_t dos2_dummy[16];
static uint32_t rs = 54321u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
int main(void)
{
    static const char *const ms[] = { "tests/machines/cbios2", "tests/machines/cbios2p", "tests/machines/cbios2_dos2fdc", "machines/C-BIOS" };
    static const int mp[] = { MAPPER_ASCII16X, MAPPER_ESESCC, MAPPER_FRSCC, MAPPER_FSCA1, MAPPER_KONAMI_SCC, MAPPER_LODERUNNER, MAPPER_MATRAINK, MAPPER_MEGASCSI,
        MAPPER_MFRSCC, MAPPER_MFRSCCP, MAPPER_NEO16, MAPPER_NEO8, MAPPER_PLAIN, MAPPER_SCCPLUS, MAPPER_SFG, MAPPER_SUNRISE, MAPPER_WAVESCSI, MAPPER_YAMANOOTO, MAPPER_KONAMI, MAPPER_BEERIDE };
    char err[256]; int states = 0, machines = 0; long chains = 0; long seen[16] = {0};
    for (unsigned i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        if (msx_host_init(&m, ms[i], err, sizeof err)) continue;
        machines++; mix(i);
        for (int s = 0; s < 3000; s++) {
            uint32_t r = rnd();
            m.fdc_kind = (r & 1) ? 63 : (r >> 1) & 1; m.fdc_ps = (r >> 2) & 3; m.fdc_ss = (r >> 4) & 3;
            m.dos2 = ((r >> 6) & 1) ? dos2_dummy : 0; m.dos2_ps = (r >> 7) & 3; m.dos2_ss = (r >> 9) & 3; m.dos2_tr = (r >> 11) & 1;
            m.fw.kind = (r >> 12) & 3; m.fw.ps = (r >> 14) & 3; m.fw.ss = (r >> 16) & 3; m.fw.start = (r >> 18) & 3; m.fw.count = 1 + ((r >> 20) & 3); m.fw.bun_ps = (int)((r >> 22) & 7) - 2; m.fw.bun_ss = (r >> 25) & 3;
            for (int n = 0; n < MSX_CART_ALL; n++) {
                uint32_t q = rnd(); Cartridge *c = &m.cart[n];
                c->present = (q & 3) != 0; c->mapper = (MapperType)mp[(q >> 2) % (sizeof mp / sizeof mp[0])];
                m.cart_slot[n] = (q >> 8) & 3; m.cart_ss[n] = (q >> 10) & 3;
                c->scc_mode = (uint8_t)(q >> 12); c->y_en = (uint8_t)(q >> 24); c->ide_ctl = (uint8_t)(q >> 25); c->mp_cfg = (uint8_t)(q >> 16); c->y_cfg = (uint8_t)(q >> 20);
                for (int k = 0; k < 4; k++) { uint32_t b = rnd(); c->bank[k] = (uint8_t)b; c->xbank[k] = (uint8_t)(b >> 8); c->mp_sccb[k] = (uint8_t)(b >> 16); c->y_raw[k] = (uint8_t)(b >> 24); }
            }
            msx_io_write(&m, 0xA8, (uint8_t)rnd());     /* 슬롯을 바꾸면 칸 사슬을 다시 만듦 */
            for (int b = 0; b < 8; b++)
                for (int w = 0; w < 2; w++) {
                    uint8_t o[16]; int n = msx_mem_owners(&m, b, w, o, 16);
                    mix((uint64_t)n << 16 | (uint64_t)b << 8 | (uint64_t)w); if (n) chains++; for (int k = 0; k < n; k++) { mix(o[k]); seen[o[k] & 15]++; }
                }
            for (int b = 0; b < 8; b++) for (int k = 0; k < 4; k++) mix(msx_mem_read(&m, (uint16_t)(b * 0x2000 + (rnd() & 0x1FFF))));
            states++;
        }
    }
    fprintf(stderr, "nonempty chains=%ld dev:", chains); for (int q = 0; q < 12; q++) fprintf(stderr, " %ld", seen[q]); fprintf(stderr, "\n");
    printf("memchain machines=%d states=%d hash=%016llx\n", machines, states, (unsigned long long)h);
    return 0;
}
