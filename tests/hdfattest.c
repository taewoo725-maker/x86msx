/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* hdfattest - 8.0.110 하드 이미지 FAT16 읽기 (kernel/hdfat.c). 이미지는 tests/hdfattest.sh 가 만듦 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/hdfat.h"
static FILE *fp;
static int rd(void *c, uint32_t lba, uint8_t *b) { (void)c; if (fseek(fp, (long)lba * 512, SEEK_SET)) return -1; return fread(b, 1, 512, fp) == 512 ? 0 : -1; }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    fp = fopen(argv[1], "rb"); if (!fp) { printf("no image\n"); return 2; }
    uint32_t st = 0, ns = 0; int part = atoi(argv[2]);
    static HdFat f;
    if (part >= 0) { CHECK(hf_part(rd, 0, 0, &st, &ns), "파티션 0"); CHECK(!hf_part(rd, 0, 1, &st, &ns) || 1, "파티션 1"); hf_part(rd, 0, 0, &st, &ns); }
    int o = hf_open(&f, rd, 0, st, ns);
    if (argc > 3) CHECK(f.fat_bits == atoi(argv[3]), "FAT%d 로 열림 (%d)", atoi(argv[3]), f.fat_bits);
    CHECK(o == 0, "hf_open");
    uint32_t sz = 0;
    CHECK(hf_find(&f, "BIG.BIN", &sz) && (sz == 3000000 || sz == 300000), "루트 큰 파일 %u", sz);
    { uint8_t b[4096]; for (uint32_t off = 0; off + 4096 <= sz; off += 99991) { if (hf_read(&f, off, b, 4096)) { CHECK(0, "읽기 실패 %u", off); break; } for (int i = 0; i < 4096; i++) if (b[i] != (uint8_t)((off + i) * 7 + ((off + i) >> 8))) { CHECK(0, "내용 다름 off %u i %d", off, i); goto nxt; } }
      nxt:; }
    { uint8_t b[10]; CHECK(!hf_read(&f, 0, b, 10) && !hf_read(&f, sz - 10, b, 10), "처음·끝 읽기"); CHECK(hf_read(&f, sz - 5, b, 10), "끝 넘어 읽기는 오류"); }
    { uint8_t b[600]; CHECK(!hf_read(&f, 1001, b, 600), "섹터 걸치는 읽기"); for (int i = 0; i < 600; i++) if (b[i] != (uint8_t)((1001 + i) * 7 + ((1001 + i) >> 8))) { CHECK(0, "걸친 읽기 내용 %d", i); break; } }
    CHECK(hf_find(&f, "SCREEN\\SHORT.MP4", &sz) && sz == 1234, "폴더 안 짧은 이름 %u", sz);
    CHECK(hf_find(&f, "screen\\short.mp4", &sz), "대소문자 무시");
    CHECK(hf_find(&f, "SCREEN\\14035-254146872_medium.mp4", &sz) && sz == 5000, "긴 이름 %u", sz);
    CHECK(!hf_find(&f, "SCREEN\\NOPE.MP4", &sz), "없는 파일");
    CHECK(!hf_find(&f, "SCREEN", &sz), "폴더는 파일 아님");
    CHECK(!hf_find(&f, "NODIR\\X.MP4", &sz), "없는 폴더");
    printf("hdfattest: %s\n", fails ? "FAIL" : "all passed");
    return fails ? 1 : 0;
}
