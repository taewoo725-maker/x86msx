/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 5.2.118 I/O 사슬 동등성 시험: 장치 표(DeviceOps) 로 옮기기 전후에 포트 256개마다 사슬(번호)이 같아야 함.
 * 기종 5개 × 기능을 켜고 끄는 무작위 상태(고정 시드) 마다 사슬을 만들어 해시 → tests/data/iochain_expected.txt 와 비교 */
#include "msx_host.h"
#include <stdio.h>
#include <string.h>
static MSX m;
static uint64_t h = 1469598103934665603ull;
static void mix(uint64_t v) { h ^= v; h *= 1099511628211ull; }
static uint32_t rs = 12345u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static uint8_t dummy_v9[16];
int main(void)
{
    static const char *const ms[] = { "tests/machines/cbios2", "tests/machines/cbios2p", "tests/machines/cbios2_dos2fdc", "machines/C-BIOS" };
    static const int mp[] = { MAPPER_BEERIDE, MAPPER_GOUDA, MAPPER_GIDE, MAPPER_ZEMINA90, MAPPER_MEGARAM, MAPPER_HBI55, MAPPER_RS232, MAPPER_SFG, MAPPER_SUNRISE, MAPPER_KONAMI };
    char err[256]; int states = 0, machines = 0;
    for (unsigned i = 0; i < sizeof ms / sizeof ms[0]; i++) {
        if (msx_host_init(&m, ms[i], err, sizeof err)) continue;
        machines++; mix(i);
        for (int s = 0; s < 3000; s++) {
            uint32_t r = rnd();
            m.net.enabled = r & 1; m.v9 = (r & 2) ? (V9990 *)(void *)dummy_v9 : 0; m.vhd_on = (r >> 2) & 1; m.ide_cart_io = (r >> 3) & 1;
            m.fdc_kind = (r >> 4) & 1 ? 63 : (r >> 5) & 1 ? 1 : 0;
            m.prof.s1990 = (r >> 6) & 1; m.prof.trtimer = (r >> 7) & 1; m.prof.f4 = (r >> 8) & 3; m.prof.trpause = (r >> 10) & 1;
            m.prof.pcm = (r >> 11) & 1; m.prof.midi = (r >> 12) & 1; m.prof.matsushita = (r >> 13) & 1;
            for (int n = 0; n < 4; n++) { uint32_t q = rnd(); m.cart[n].present = q & 1; m.cart[n].mapper = (MapperType)mp[(q >> 1) % (sizeof mp / sizeof mp[0])]; m.cart[n].rs.uart = (q >> 8) % 3; }
            msx_io_rebuild(&m);
            for (int p = 0; p < 256; p++) { uint8_t o[MSX_IO_CHAIN]; int n = msx_io_owners(&m, p, o); mix((uint64_t)n << 8 | (uint64_t)p); for (int k = 0; k < n; k++) mix(o[k]); }
            states++;
        }
    }
    printf("iochain machines=%d states=%d hash=%016llx\n", machines, states, (unsigned long long)h);
    return 0;
}
