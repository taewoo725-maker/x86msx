/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 5.2.33 영속 데이터 선언 시험 (src/persist.c) — 파일 이름·꼴이 예전 판(fe_state.inc sram_kind/sram_path)과 같아야 함
 * (사용자 USB 의 SRAM\ 파일을 그대로 읽어야 하므로) */
#include "../src/persist.h"
#include "../src/media.h"
#include "msx_host.h"
#include <stdio.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static MSX m;
static uint8_t rom[512 * 1024];

static void put_cart(int n, MapperType mt, uint32_t size)
{
    memset(&m.cart[n], 0, sizeof m.cart[n]);
    m.cart[n].present = 1; m.cart[n].mapper = mt; m.cart[n].data = rom; m.cart[n].size = size;
}
/* 이름 + 꼴 + 크기 */
static void expect(int n, int has_rom, int special, int ok, int fmt, int from_rom, const char *name, uint32_t size, const char *what)
{
    MsxPersist p;
    int r = msx_cart_persist(&m, n, has_rom, special, &p);
    CHECK(r == ok, "%s: ok %d want %d", what, r, ok);
    if (!r || !ok) return;
    CHECK(p.fmt == fmt && p.from_rom == from_rom && !strcmp(p.name, name) && p.size == size, "%s: fmt %d rom %d name '%s' size %u", what, p.fmt, p.from_rom, p.name, (unsigned)p.size);
}

int main(void)
{
    char err[256];
    if (msx_host_init(&m, "tests/machines/cbios2", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    put_cart(0, MAPPER_FMPAC, 65536); expect(0, 1, 1, 1, PS_PAC, 0, "FMPAC.pac", 8190, "fmpac");
    put_cart(MSX_INT_CART, MAPPER_FMPAC, 65536); expect(MSX_INT_CART, 0, 0, 1, PS_PAC, 0, "FMPAC_builtin.pac", 8190, "fmpac builtin");
    put_cart(1, MAPPER_PAC, 8192); expect(1, 0, 1, 1, PS_PAC, 0, "PAC.pac", 8190, "pac");
    put_cart(MSX_INT_CART, MAPPER_PAC, 8192); expect(MSX_INT_CART, 0, 0, 1, PS_PAC, 0, "PAC_builtin.pac", 8190, "pac builtin");
    put_cart(0, MAPPER_ASCII8_SRAM, 131072); expect(0, 1, 0, 1, PS_RAW, 1, ".sram", 8192, "ascii8 sram");
    m.cart[0].sram_kb = 32; expect(0, 1, 0, 1, PS_RAW, 1, ".sram", 32768, "ascii8 sram 32K");
    put_cart(0, MAPPER_ASCII16_SRAM, 131072); expect(0, 0, 0, 0, 0, 0, "", 0, "ascii16 sram no rom path");
    put_cart(MSX_INT_CART, MAPPER_GAMEMASTER2, 131072); expect(MSX_INT_CART, 0, 0, 0, 0, 0, "", 0, "gm2 builtin");
    put_cart(2, MAPPER_GAMEMASTER2, 131072); expect(2, 1, 0, 1, PS_RAW, 1, ".sram", 8192, "gm2");
    put_cart(0, MAPPER_ASCII16X, 524288); expect(0, 1, 0, 1, PS_FLASH, 1, ".flash", 524288, "ascii16x");
    put_cart(0, MAPPER_YAMANOOTO, 524288); expect(0, 1, 0, 1, PS_FLASH, 1, ".flash", 524288, "yamanooto");
    put_cart(0, MAPPER_YAMANOOTO, 8u << 20); expect(0, 0, 1, 1, PS_FLASH, 0, "Yamanooto.flash", 8u << 20, "yamanooto empty cart (5.2.43)");
    put_cart(1, MAPPER_ASCII16X, 8u << 20); expect(1, 0, 1, 1, PS_FLASH, 0, "ASCII16-X.flash", 8u << 20, "ascii16x empty cart");
    put_cart(1, MAPPER_ASCII16X, 8u << 20); expect(1, 0, 0, 0, 0, 0, "", 0, "ascii16x builtin none");
    put_cart(1, MAPPER_HBI55, 16384); expect(1, 0, 1, 1, PS_RAW, 0, "HBI-55.sram", 4096, "hbi-55 (5.2.45)");
    put_cart(0, MAPPER_MFRSCCP, 1u << 20); expect(0, 0, 1, 1, PS_RAW, 0, "MegaFlashROM_SCCplus.flash", 1u << 20, "mfr scc+ (5.2.46)");
    put_cart(0, MAPPER_FRSCC, 16u << 20); expect(0, 0, 1, 1, PS_FLASH, 0, "FlashROM_SCC.flash", 16u << 20, "flash-rom scc (5.2.47)");
    put_cart(MSX_INT_CART, MAPPER_HBI55, 16384); expect(MSX_INT_CART, 0, 0, 1, PS_RAW, 0, "HBI-55_builtin.sram", 4096, "hbi-55 builtin");
    put_cart(0, MAPPER_FSCA1, 65536); expect(0, 1, 1, 1, PS_RAW, 0, "FS-CA1.sram", 8192, "fsca1");
    put_cart(0, MAPPER_ESERAM, 524288); expect(0, 0, 1, 1, PS_RAW, 0, "EseRAM_512KB.sram", 524288, "eseram");
    put_cart(0, MAPPER_ESESCC, 262144); expect(0, 0, 1, 1, PS_RAW, 0, "EseSCC_256KB.sram", 262144, "esescc");
    put_cart(0, MAPPER_MEGASCSI, 524288); expect(0, 0, 1, 1, PS_RAW, 0, "MegaSCSI_512KB.sram", 524288, "megascsi");
    put_cart(0, MAPPER_WAVESCSI, 131072); expect(0, 0, 1, 1, PS_RAW, 0, "WaveSCSI_128KB.sram", 131072, "wavescsi");
    put_cart(0, MAPPER_MFRSCC, 524288); expect(0, 0, 1, 1, PS_RAW, 0, "MegaFlashROM_SCC.flash", 524288, "mfr");
    put_cart(0, MAPPER_MATRAINK, 65536); expect(0, 0, 1, 1, PS_RAW, 0, "EseRAM_64KB.sram", 65536, "matra (예전 이름 그대로)");
    put_cart(0, MAPPER_ESERAM, 524288); expect(0, 1, 0, 1, PS_RAW, 1, ".sram", 524288, "rom file as eseram");
    put_cart(0, MAPPER_KONAMI, 131072); expect(0, 1, 0, 0, 0, 0, "", 0, "konami none");
    m.cart[3].present = 0; expect(3, 1, 0, 0, 0, 0, "", 0, "empty");
    {   /* 펌웨어 SRAM */
        MsxFwPersist f;
        m.fw.kind = 0; m.prof.matsushita = 0; CHECK(msx_fw_persist(&m, &f) == 0, "fw none");
        m.prof.matsushita = 1; CHECK(msx_fw_persist(&m, &f) == 2048 && f.size[0] == 0, "matsushita only");
        m.fw.kind = 1; m.fw.sram_size = 16384; CHECK(msx_fw_persist(&m, &f) == 16384 + 2048 && f.data[0] == m.fw.sram && f.data[1] == m.fw.mat_sram, "fw + mat");
    }
    {   /* 5.2.36 미디어 자리 */
        static MSX t; MsxMediaSlots ms;
        if (!msx_host_init(&t, "tests/machines/cbios2", err, sizeof err)) { msx_media_slots(&t, &ms); CHECK(ms.tape == 1 && ms.disk == 0 && ms.cart[0] && ms.cart[1], "cbios2 slots tape %d disk %d", ms.tape, ms.disk); }
        if (!msx_host_init(&t, "tests/machines/cbios2_dos2fdc", err, sizeof err)) { msx_media_slots(&t, &ms); CHECK(ms.disk >= 1, "fdc machine disk %d", ms.disk); }
        if (!msx_host_init(&t, "tests/regress/local/machines/a1gt", err, sizeof err)) { msx_media_slots(&t, &ms); CHECK(ms.tape == 0, "turbo R no tape"); }   /* 개인 롬이 있을 때만 */
    }
    printf("persisttest: %d/%d passed\n", checks - fails, checks);
    return fails != 0;
}
