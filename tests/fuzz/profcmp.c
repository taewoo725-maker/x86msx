/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - 기종 설정(config.ini) 해석 동등성 시험 (5.2.8, tests/fuzz/fuzzcmp.sh 가 이전 판과 비교)
 * usage: profcmp <config.ini 들이 있는 폴더 …> — 실제 기종 파일 + 형식 번호 × 블록 수 × 파일 있음/없음 합성 줄을 해석해 해시 */
#define _DEFAULT_SOURCE
#include "profile.h"
#include "../kernel/medit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
static uint64_t hash(const void *p, size_t n) { uint64_t h = 1469598103934665603ull; const uint8_t *b = p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } return h; }
static MachineProfile mp;
static void one(const char *name, const char *text)
{
    char err[256] = "";
    memset(&mp, 0, sizeof mp);
    int r = profile_parse(text, strlen(text), &mp, err, sizeof err);
    printf("%s r=%d err=[%s] %016llx\n", name, r, err, (unsigned long long)hash(&mp, sizeof mp));
}
int main(int argc, char **argv)
{
    static char buf[1 << 16];
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
                for (int st = 0; st < 3; st++) {
                    char text[512], name[64];
                    snprintf(text, sizeof text, "[Slots]\n0 0 0 4 20 \"bios.rom\" \"\"\n%d %d %d %d %d \"%s\" \"\"\n%s", st == 2 ? 3 : 1, st == 2 ? 1 : 0, st == 1 ? 2 : 0, counts[c], t, fl ? "x.rom" : "",
                             t == 33 ? "[Subslotted Slots]\nslot 3=1\n" : "");
                    snprintf(name, sizeof name, "t%d c%d f%d s%d", t, counts[c], fl, st);
                    one(name, text);
                }
    for (int t = -2; t < 260; t++) {
        uint32_t sz; uint8_t fill; uint32_t s2; uint8_t f2;
        profile_cart_buf(t, 4, &sz, &fill); profile_cart_buf(t, 32, &s2, &f2);
        const MeTypeInfo *ti = me_type_info(t);
        printf("type %d map %d sup %d buf %u/%u %u/%u me %s %d %d io %d rom %d win %d,%d,%d\n", t, profile_cart_mapper(t), profile_type_supported(t), sz, fill, s2, f2,
               ti ? ti->name : "-", ti ? ti->kind : -1, ti ? ti->win : -1, me_type_io(t), me_type_rom(t), me_window(t, 0), me_window(t, 3), me_window(t, 12));
    }
    printf("me_count %d\n", me_type_count());
    for (int i = 0; i < me_type_count(); i++) printf("me %d %d\n", i, me_type_at(i)->t);
    return 0;
}
