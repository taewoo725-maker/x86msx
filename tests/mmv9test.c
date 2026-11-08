/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
#define _POSIX_C_SOURCE 200809L
/* 8.0.131 시험: X86 Video 가 MP4 를 V9990(Gfx9000) VRAM 에 직접 그린다 (화면 13 = B2 16비트, 14 = B4 256색) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../src/msx.h"
#include "../src/mmvid.h"
#include "../src/v9990.h"
#include "../src/vsrc.h"
#include "../host/msx_host.h"
static MSX m; static int fails; static V9990 *v9;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
#define ROOT "/tmp/mmv9_test/Video"
static int h_list(const char *dir, MmEnt *out, int max)
{ DIR *d = opendir(dir); if (!d) return -1; struct dirent *e; int k = 0;
  while ((e = readdir(d)) && k < max) { char p[512]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name); struct stat st; if (stat(p, &st)) continue;
    snprintf(out[k].name, sizeof out[k].name, "%s", e->d_name); out[k].is_dir = S_ISDIR(st.st_mode); out[k].size = (uint32_t)st.st_size; k++; }
  closedir(d); return k; }
static int h_read(const char *path, uint32_t off, uint8_t *buf, uint32_t len)
{ FILE *f = fopen(path, "rb"); if (!f) return -1; fseek(f, (long)off, SEEK_SET); size_t n = fread(buf, 1, len, f); fclose(f); if (n < len) memset(buf + n, 0, len - n); return 0; }
static const MmHost host = { h_list, h_read, ROOT, '/' };
static void w(int p, int v) { msx_io_write(&m, p, (uint8_t)v); }
static int r(int p) { return msx_io_read(&m, p); }
static void cmd(int c, int n, const uint8_t *a) { w(0x24, c); for (int i = 0; i < n; i++) w(0x26, a[i]); }
static void frames(int n) { for (int i = 0; i < n; i++) msx_run_frame(&m); }
static int open_scr(int scr) { uint8_t a[12] = { 0, 0, 0, 0, (uint8_t)scr, 192, 24, 40, 1, 75, 0, 1 };   /* X86MV V1.11.15 가 보내는 값 (P9=75, P11=PAL) */ cmd(0x11, 12, a); int e = r(0x24) & 8; return e ? r(0x25) : 0; }
static int find_item(const char *name) { for (int i = 0; i < 40; i++) { uint8_t a[4] = { 0, 0, (uint8_t)i, 0 }; cmd(0x10, 4, a); uint8_t b[15]; for (int k = 0; k < 15; k++) b[k] = (uint8_t)r(0x25); if (b[0] == 0xFF) return -1; if (b[0] == 0 && !strncmp((char *)b + 1, name, strlen(name)) && !b[1 + strlen(name)]) return i; } return -1; }
static int open_scr_file(const char *name, int scr) { int it = find_item(name); if (it < 0) return -99; uint8_t a[12] = { 0, 0, (uint8_t)it, (uint8_t)(it >> 8), (uint8_t)scr, 192, 24, 40, 1, 75, 0, 1 }; cmd(0x11, 12, a); int e = r(0x24) & 8; return e ? r(0x25) : 0; }
static int nonzero(int n) { int c = 0; for (int i = 0; i < n; i++) if (v9->vram[i]) c++; return c; }
static int is_green(uint32_t c) { int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255; return g > 0xA0 && r < 0x50 && b < 0xA0 && g > r + 0x60; }   /* 디더링으로 값이 조금 흔들림 */
int main(int argc, char **argv)
{
    char err[256];
    if (system("rm -rf /tmp/mmv9_test; mkdir -p " ROOT " && cd " ROOT " && ffmpeg -loglevel error -y -f lavfi -i color=c=white:size=640x360:rate=15:duration=3,format=yuv420p -c:v libx264 -profile:v baseline -an base.mp4 && ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=15:duration=3,format=yuv420p -f lavfi -i sine=frequency=440:duration=3 -c:v mjpeg -pix_fmt yuvj420p -c:a pcm_s16le m.avi")) { printf("ffmpeg 준비 실패\n"); return 1; }
    if (msx_host_init(&m, argc > 1 ? argv[1] : "tests/machines/cbios2p", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    static V9990 mem; v9 = &mem;
    mmvid_set_host(&host);
    static uint8_t blank[16384]; memset(blank, 0xFF, sizeof blank);
    CHECK(msx_insert_cart(&m, 1, blank, sizeof blank, MAPPER_MMEDIA) == 0, "insert");
    msx_reset(&m); frames(120);
    msx_mem_write(&m, 0xC000, 0xF3); msx_mem_write(&m, 0xC001, 0x18); msx_mem_write(&m, 0xC002, 0xFE); m.cpu.pc = 0xC000; frames(2);
    /* Gfx9000 없음 → 코드 6 */
    CHECK(open_scr(13) == 6, "no Gfx9000: screen 13 must give code 6");
    msx_v9990_enable(&m, &mem); frames(2);
    cmd(0x01, 0, 0); r(0x25); r(0x25); r(0x25); r(0x25); int id5 = r(0x25); CHECK(id5 & 0x40, "IDENT bit6 with Gfx9000 (%02X)", id5);
    for (int s = 13; s <= 14; s++) {
        for (int y = 0; y < V9_FBH; y++) for (int x = 0; x < V9_FBW; x++) mem.fb[y][x] = 0xFFF5DC;   /* 앞 화면 찌꺼기(크림색)가 남아 있어도 영상 줄이 다 덮는지 */
        int e = open_scr(s); CHECK(e == 0, "open screen %d code %d", s, e);
        cmd(0x03, 0, 0); frames(40);
        { w(0x24, 0x0D); for (int i = 0; i < 4096; i++) w(0x25, i < 2048 ? 0xFF : 0x10); { uint8_t on = 1; cmd(0x0E, 1, &on); } frames(3);   /* 글자 줄: 위 24줄 전부 켠 점(색 fg=흰) */
          printf("  글자 줄 켬: fb 줄 5 %06X (초록이 아니어야, 8.0.161)\n", mem.fb[5][320]); CHECK(!(is_green(mem.fb[5][320]) && is_green(mem.fb[5][200])), "screen %d: 8.0.161 파란 표시줄이 있으면 옛 초록 글자 줄은 안 얹힘", s);
          { int blue = 0, y0 = V9_FBH - 8; for (int y = 0; y < V9_FBH; y++) for (int x = 0; x < V9_FBW; x++) { uint32_t c = mem.fb[y][x], b = mmv9_bar_color(2); int ok = 1; for (int sh = 0; sh < 24; sh += 8) { int d = (int)((c >> sh) & 255) - (int)((b >> sh) & 255); if (d < 0) d = -d; if (d > 40) ok = 0; } blue += ok; }   /* 15·8비트 색이라 가까우면 됨 */
            printf("  표시줄 파란 점 %d (8.0.151, 화면 %d)\n", blue, y0 * 0 + s); CHECK(blue > 1000, "screen %d bar: bottom rows must show the blue bar (%d)", s, blue); }
          { uint8_t off = 0; cmd(0x0E, 1, &off); } frames(3); { uint32_t c = mem.fb[5][320], l = mem.fb[5][4]; int d = 0; for (int sh = 0; sh < 24; sh += 8) { int dd = (int)((c >> sh) & 255) - (int)((l >> sh) & 255); if (dd < 0) dd = -dd; if (dd > d) d = dd; }
            CHECK(d <= 40, "screen %d ovl off: the top band must be the color of the left margin (8.0.148) %06X vs %06X", s, c, l); } }
        printf("화면 %d: V9990 mode %d regs6=%02X disp_en %d VRAM 채운 바이트 %d\n", s, mem.mode, mem.regs[6], mem.disp_en, nonzero(0x80000));
        CHECK(mem.mode == (s == 13 ? V9_B2 : V9_B4), "screen %d mode %d", s, mem.mode);
        CHECK(nonzero(0x80000) > 50000, "screen %d VRAM mostly empty", s);
        CHECK(v9990_visible(&mem), "screen %d: V9990 not visible", s);
        for (int k = 0; k < 8; k++) { int ry = k < 3 ? k : k < 5 ? 100 + k : 232 + k; printf("  fb 줄 %3d 가운데 %06X 왼 %06X\n", ry, mem.fb[ry][320], mem.fb[ry][4]); }
        CHECK(msx_vsrc_pick(&m, -1) == VSRC_V9990, "screen %d: auto source must pick V9990 (got %d)", s, msx_vsrc_pick(&m, -1));
        cmd(0x05, 0, 0); frames(2);
        CHECK(mem.sysreset != 0 || !mem.disp_en, "screen %d stop must turn V9990 off", s);
    }
    /* 8.0.148: AVI(MJPEG+PCM) 도 화면 13·14 로 (예전엔 코드 6) */
    mmvid_set_host(&host);
    for (int s = 13; s <= 14; s++) {
        int e = open_scr_file("m.avi", s); CHECK(e == 0, "avi open screen %d code %d (8.0.148)", s, e);
        cmd(0x03, 0, 0); frames(40);
        CHECK(nonzero(0x80000) > 50000 && v9990_visible(&mem), "avi screen %d: V9990 shows video", s);
        cmd(0x05, 0, 0); frames(2);
    }
    printf(fails ? "mmv9test FAIL %d\n" : "mmv9test OK\n", fails);
    return fails != 0;
}
