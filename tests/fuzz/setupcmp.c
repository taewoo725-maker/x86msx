/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 기종 만들기(msx_setup) 동등성 시험 (5.2.89, tests/fuzz/fuzzcmp.sh 가 이전 판과 비교)
 * usage: setupcmp <config.ini 들이 있는 폴더 …>
 * 실제 기종 파일 + 형식 번호 × 블록 수 × 파일 있음/없음 × 슬롯 합성 줄을 해석하고, 파일이 있는 영역에 가짜 롬(고정 시드)을 넣어 msx_setup 을 돌린 뒤
 * 결과(돌려준 값, 슬롯 표 칸마다 읽기 내용·쓰기 자리, 카트 자리, FDC·DOS2·펌웨어·한자·매퍼 RAM·영역 롬)를 해시로 찍는다. */
#define _DEFAULT_SOURCE
#include "msx.h"
#include "profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
static uint64_t h;
static void hb(const void *p, size_t n) { const uint8_t *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } }
static void hv(uint64_t v) { hb(&v, sizeof v); }
static MSX m;
static MachineProfile mp;
#define RMAX (256u << 10)
static uint8_t rbuf[MAX_REGIONS][RMAX];
static RomImage roms[MAX_REGIONS];
/* 포인터 → 비교할 수 있는 값: 가짜 롬 k 의 몇 번째, 기계 구조체 안 몇 번째, 아니면 내용 해시 */
static void hp(const void *p, uint32_t len)
{
    const uint8_t *q = p;
    if (!q) { hv(1); return; }
    for (int k = 0; k < MAX_REGIONS; k++) if (q >= rbuf[k] && q < rbuf[k] + RMAX) { hv(2); hv((uint64_t)k); hv((uint64_t)(q - rbuf[k])); return; }
    if (q >= (const uint8_t *)&m && q < (const uint8_t *)&m + sizeof m) { hv(3); hv((uint64_t)(q - (const uint8_t *)&m)); return; }
    hv(4); hb(q, len);
}
static uint32_t rsz(const ProfileRegion *r)
{
    switch (r->type) {
    case REGION_KANJI: return 131072;
    case REGION_JISYO: case REGION_ROM_DOS2: return 65536;
    case REGION_DISK_ROM: return r->blue_type == 143 ? 65536 : (uint32_t)r->block_count * 8192;
    case REGION_CART: return r->block_count * 8192u > 32768 ? r->block_count * 8192u : 32768;
    default: return r->block_count ? (uint32_t)r->block_count * 8192 : 8192;
    }
}
static void one(const char *name, const char *text)
{
    char err[256] = "";
    memset(&mp, 0, sizeof mp);
    int pr = profile_parse(text, strlen(text), &mp, err, sizeof err);
    if (pr) { printf("%s parse %d\n", name, pr); return; }
    for (int i = 0; i < MAX_REGIONS; i++) { roms[i].data = 0; roms[i].size = 0; }
    for (int i = 0; i < mp.region_count && i < MAX_REGIONS; i++) {
        const ProfileRegion *r = &mp.region[i];
        if (!r->file[0]) continue;
        uint32_t s = rsz(r); if (s > RMAX) s = RMAX;
        uint32_t x = 2463534242u + (uint32_t)i * 977u;
        for (uint32_t k = 0; k < s; k++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; rbuf[i][k] = (uint8_t)x; }
        roms[i].data = rbuf[i]; roms[i].size = s;
    }
    int r = msx_setup(&m, &mp, roms);
    h = 1469598103934665603ull;
    hv((uint64_t)(int64_t)r);
    if (!r) {
        for (int s = 0; s < 4; s++) for (int ss = 0; ss < 4; ss++) for (int b = 0; b < 8; b++) { hp(m.slot_rd[s][ss][b], 8192); hp(m.slot_wr[s][ss][b], 0); }
        for (int n = 0; n < MSX_CART_ALL; n++) {
            const Cartridge *c = &m.cart[n];
            hv(c->present); hv(c->mapper); hv(c->size); hv(c->banks8); hv(c->plain_base); hv((uint64_t)m.cart_slot[n]); hv((uint64_t)m.cart_ss[n]);
            if (c->present) hp(c->data, c->size < 8192 ? c->size : 8192);
        }
        hv((uint64_t)m.fdc_kind); hv((uint64_t)m.fdc_ps); hv((uint64_t)m.fdc_ss);
        hp(m.dos2, 64); hv(m.dos2_size); hv((uint64_t)m.dos2_ps); hv((uint64_t)m.dos2_ss); hv((uint64_t)m.dos2_tr);
        hv(m.mram_size); hv((uint64_t)m.mapper_ps); hv((uint64_t)m.mapper_ss);
        hp(m.kanji, 64); hv(m.kanji_size); hv(m.kanji_hi_mask);
        hv((uint64_t)m.fw.kind); hp(m.fw.rom, 64); hv(m.fw.rom_size); hv((uint64_t)m.fw.ps); hv((uint64_t)m.fw.ss); hv((uint64_t)m.fw.start); hv((uint64_t)m.fw.count);
        hv(m.fw.sram_size); hv(m.fw.sram_mirror); hv(m.fw.ram_banks); hv((uint64_t)m.fw.bun_ps); hv((uint64_t)m.fw.bun_ss); hp(m.fw.jisyo, 64); hv(m.fw.jisyo_size);
        for (int i = 0; i < MAX_REGIONS; i++) { hp(m.rom[i].data, 64); hv(m.rom[i].size); }
        hv(m.scr.int_pool_used); hb(m.scr.int_pool, m.scr.int_pool_used < 65536 ? m.scr.int_pool_used : 65536);
        hv((uint64_t)m.rtc_on); hv((uint64_t)m.s1985);
    }
    printf("%s r=%d %016llx\n", name, r, (unsigned long long)h);
}
int main(int argc, char **argv)
{
    static char buf[1 << 16];
    setvbuf(stdout, 0, _IOFBF, 1 << 16);
    for (int a = 1; a < argc; a++) {
        DIR *d = opendir(argv[a]); if (!d) continue;
        struct dirent *e; char *names[1024]; int n = 0;
        while ((e = readdir(d)) && n < 1024) if (strstr(e->d_name, ".ini")) names[n++] = strdup(e->d_name);
        closedir(d);
        for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (strcmp(names[i], names[j]) > 0) { char *t = names[i]; names[i] = names[j]; names[j] = t; }
        for (int i = 0; i < n; i++) {
            char path[2048]; snprintf(path, sizeof path, "%s/%s", argv[a], names[i]);
            FILE *f = fopen(path, "rb"); if (!f) continue;
            size_t k = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[k] = 0;
            one(names[i], buf);
        }
    }
    static const int counts[] = { 0, 1, 2, 4, 6, 8, 16, 64 };
    for (int t = -2; t < 200; t++)
        for (unsigned c = 0; c < sizeof counts / sizeof counts[0]; c++)
            for (int fl = 0; fl < 2; fl++)
                for (int st = 0; st < 4; st++) {
                    char text[512], name[64];
                    snprintf(text, sizeof text, "[Slots]\n0 0 0 4 20 \"bios.rom\" \"\"\n3 0 0 8 23 \"\" \"\"\n%d %d %d %d %d \"%s\" \"\"\n%s",
                             st >= 2 ? 3 : 1, st >= 2 ? 1 : 0, st == 1 ? 2 : st == 3 ? 4 : 0, counts[c], t, fl ? "x.rom" : "",
                             st >= 2 ? "[Subslotted Slots]\nslot 3=1\n" : "");
                    snprintf(name, sizeof name, "t%d c%d f%d s%d", t, counts[c], fl, st);
                    one(name, text);
                }
    return 0;
}
