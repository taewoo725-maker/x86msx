/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 플래시 칩 부품 단독 시험 (5.2.7, src/flash.c) */
#include "flash.h"
#include <stdio.h>
#include <string.h>
static int fails, checks;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; printf("FAIL %s (line %d)\n", msg, __LINE__); } } while (0)
static uint8_t buf[8u << 20], map[256], dirty;
static void cmd(FlashChip *f, FlashMem *m, uint32_t a1, uint32_t a2, uint8_t c) { flash_write(f, m, a1, 0xAA); flash_write(f, m, a2, 0x55); flash_write(f, m, a1, c); }
int main(void)
{
    FlashChip f; FlashMem m = { buf, 512u << 10, &dirty, map, sizeof map };
    /* AM29F040 */
    flash_init(&f, FLASH_AM29F040);
    memset(buf, 0xFF, sizeof buf);
    CHECK(flash_chip_size(FLASH_AM29F040) == 512u << 10, "29F040 크기");
    CHECK(flash_id_page(&f) == 0, "읽기 모드면 ID 없음");
    flash_write(&f, &m, 0x555, 0xAA); flash_write(&f, &m, 0x2AA, 0x55);
    CHECK(flash_write(&f, &m, 0x555, 0x90) == 1 && f.mode == 1, "자동 선택 들어감");
    CHECK(flash_id_page(&f)[0] == 0x01 && flash_id_page(&f)[1] == 0xA4, "ID 01/A4");
    CHECK(flash_write(&f, &m, 0x1234, 0xF0) == 1 && f.mode == 0, "F0 으로 나옴");
    cmd(&f, &m, 0x555, 0x2AA, 0xA0); flash_write(&f, &m, 0x12345, 0x5A);
    CHECK(buf[0x12345] == 0x5A && dirty == 1, "바이트 쓰기");
    cmd(&f, &m, 0x555, 0x2AA, 0xA0); flash_write(&f, &m, 0x12345, 0xF0);
    CHECK(buf[0x12345] == 0x5A, "F0 은 늘 되돌리기 (쓰기 아님)");
    cmd(&f, &m, 0x555, 0x2AA, 0xA0); flash_write(&f, &m, 0x12345, 0xA5);
    CHECK(buf[0x12345] == 0x00, "1 → 0 만 (AND)");
    CHECK(map[0] == 0, "29F040 은 조각 표시 안 함");
    cmd(&f, &m, 0x1555, 0x2AA, 0xA0); flash_write(&f, &m, 0x20000, 0x00);
    CHECK(buf[0x20000] == 0x00, "명령 주소는 아래 11비트만");
    cmd(&f, &m, 0x555, 0x2AA, 0x80); flash_write(&f, &m, 0x555, 0xAA); flash_write(&f, &m, 0x2AA, 0x55); flash_write(&f, &m, 0x13000, 0x30);
    CHECK(buf[0x12345] == 0xFF && buf[0x20000] == 0x00, "섹터 64 KiB 지우기");
    cmd(&f, &m, 0x555, 0x2AA, 0x80); flash_write(&f, &m, 0x555, 0xAA); flash_write(&f, &m, 0x2AA, 0x55); flash_write(&f, &m, 0x555, 0x10);
    CHECK(buf[0x20000] == 0xFF, "칩 전체 지우기");
    { FlashMem s = { buf, 32768, &dirty, 0, 0 }; buf[32768] = 0x77; buf[100] = 0;
      cmd(&f, &s, 0x555, 0x2AA, 0x80); flash_write(&f, &s, 0x555, 0xAA); flash_write(&f, &s, 0x2AA, 0x55); flash_write(&f, &s, 0x100, 0x30);
      CHECK(buf[100] == 0xFF && buf[32768] == 0x77, "버퍼가 섹터보다 작으면 버퍼까지만 지움"); }
    /* S29GL064N (Yamanooto) */
    m.size = 8u << 20; memset(map, 0, sizeof map); dirty = 0;
    flash_init(&f, FLASH_S29GL064N);
    CHECK(flash_chip_size(FLASH_S29GL064N) == 8u << 20, "S29GL064 크기 8 MiB");
    cmd(&f, &m, 0xAAA, 0x555, 0xA0); flash_write(&f, &m, 0x7F0001, 0x12);
    CHECK(buf[0x7F0001] == 0x12 && (map[0x7F0001 >> 16] & (1u << ((0x7F0001 >> 13) & 7))), "바이트 쓰기 + 8 KiB 조각 표시");
    cmd(&f, &m, 0xAAB, 0x554, 0xA0); flash_write(&f, &m, 0x3000, 0x00);
    CHECK(buf[0x3000] == 0x00, "워드 모드 주소 AABh/554h");
    buf[0x5000] = 0;
    cmd(&f, &m, 0xAAA, 0x555, 0x80); flash_write(&f, &m, 0xAAA, 0xAA); flash_write(&f, &m, 0x555, 0x55); flash_write(&f, &m, 0x2100, 0x30);
    CHECK(buf[0x3000] == 0xFF && buf[0x5000] == 0x00, "처음 64 KiB 는 8 KiB 섹터");
    cmd(&f, &m, 0xAAA, 0x555, 0x90);
    CHECK(f.mode == 1 && flash_id_page(&f)[0] == 0x01 && flash_id_page(&f)[2] == 0x7E, "자동 선택 ID 01/7E");
    flash_write(&f, &m, 0xAA, 0x98);
    CHECK(f.mode == 2 && flash_id_page(&f)[0x20] == 'Q' && flash_id_page(&f)[0x22] == 'R' && flash_id_page(&f)[0x24] == 'Y', "CFI QRY");
    flash_write(&f, &m, 0, 0xFF);
    CHECK(f.mode == 2, "N형: CFI 중 FFh 로 안 나옴");
    flash_write(&f, &m, 0, 0xF0);
    CHECK(f.mode == 0, "F0 으로 나옴");
    /* S29GL064S (ASCII16-X) */
    flash_init(&f, FLASH_S29GL064S);
    flash_write(&f, &m, 0xAA, 0x98);
    CHECK(f.mode == 2 && flash_id_page(&f)[0] == 0x01, "S형 CFI 머리 (제조사 01)");
    flash_write(&f, &m, 0, 0xFF);
    CHECK(f.mode == 0, "S형: CFI 중 FFh 로 나옴");
    flash_reset(&f);
    CHECK(f.mode == 0 && f.step == 0, "리셋");
    printf("flashtest: %d/%d passed\n", checks - fails, checks);
    return fails != 0;
}
