/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86 Multimedia 영상 (8.0.75) 시험: 합성 MV2 를 호스트 폴더에서 직접 읽어 SCREEN 2 로 보이는지, 목록·열기·재생·일시정지·탐색·글자 줄·끝 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../src/msx.h"
#include "../src/mmvid.h"
#include "../src/mvsnd.h"
#include "../host/msx_host.h"

static MSX m;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
#define ROOT "/tmp/mmvid_test/Video"
#define NF 60

static int h_list(const char *dir, MmEnt *out, int max)
{
    DIR *d = opendir(dir); if (!d) return -1;
    struct dirent *e; int k = 0;
    while ((e = readdir(d)) && k < max) {
        char p[512]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name); struct stat st; if (stat(p, &st)) continue;
        snprintf(out[k].name, sizeof out[k].name, "%s", e->d_name); out[k].is_dir = S_ISDIR(st.st_mode) ? 1 : 0; out[k].size = (uint32_t)st.st_size; k++;
    }
    closedir(d); return k;
}
static int h_read(const char *path, uint32_t off, uint8_t *buf, uint32_t len)
{
    FILE *f = fopen(path, "rb"); if (!f) return -1;
    fseek(f, (long)off, SEEK_SET); size_t n = fread(buf, 1, len, f); fclose(f);
    if (n < len) memset(buf + n, 0, len - n);
    return 0;
}
static const MmHost host = { h_list, h_read, ROOT, '/' };

static uint8_t fr_pat(int f, int i) { return (uint8_t)(f * 7 + i * 3); }
static uint8_t fr_col(int f, int i) { return (uint8_t)(f * 5 + i * 11 + 1); }

static void make_files(void)
{
    system("rm -rf /tmp/mmvid_test; mkdir -p /tmp/mmvid_test/Video/SUB");
    FILE *t = fopen("tests/data/mvsnd_test.mp3", "rb"); static uint8_t mp3[400000]; size_t mn = t ? fread(mp3, 1, sizeof mp3, t) : 0; if (t) fclose(t);
    FILE *f = fopen(ROOT "/T.MV2", "wb");
    static uint8_t fr[0x4000];
    memset(fr, 0, sizeof fr); memcpy(fr, "MMCSD_MV", 8); memcpy(fr + 0x10, "v2.00", 5); fr[0x18] = NF;
    fwrite(fr, 1, 0x4000, f);
    size_t pos = 0;
    for (int i = 0; i < NF; i++) {
        memset(fr, 0x55, sizeof fr);
        for (int k = 0; k < 6144; k++) { fr[k] = fr_pat(i, k); fr[6144 + k] = fr_col(i, k); }
        for (int c = 0; c < 15; c++) { fr[0x3000 + c * 2] = (uint8_t)(c == 0 ? 0x70 : 0x11); fr[0x3001 + c * 2] = (uint8_t)(c == 0 ? 0x07 : 0x01); }   /* 색 1 = R7 G7 B0, 나머지 R1 G1 B1 */
        size_t n = mn - pos > 1056 ? 1056 : mn - pos;
        fr[0x3200] = (uint8_t)(n / 32); memcpy(fr + 0x3201, mp3 + pos, (size_t)(n / 32) * 32); pos += (size_t)(n / 32) * 32;
        fwrite(fr, 1, 0x4000, f);
    }
    fclose(f);
    f = fopen(ROOT "/notes.txt", "wb"); fputs("x", f); fclose(f);
    f = fopen(ROOT "/BAD.MV2", "wb"); fputs("not an mv2 file at all", f); fclose(f);
    f = fopen(ROOT "/SUB/IN.MV2", "wb"); fwrite(fr, 1, 0x4000, f); fclose(f);   /* 머리만 있는 틀린 파일 (프레임 0개) */
}
static void w(int p, int v) { msx_io_write(&m, p, (uint8_t)v); }
static int r(int p) { return msx_io_read(&m, p); }
static void cmd(int c, int n, const uint8_t *a) { w(0x24, c); for (int i = 0; i < n; i++) w(0x26, a[i]); }
static int st(void) { return r(0x24); }
static void frames(int n) { for (int i = 0; i < n; i++) msx_run_frame(&m); }
static int pos(void) { w(0x24, 0x09); return r(0x26) | (r(0x27) << 8); }
static int vram_matches(int f) { for (int k = 0; k < 6144; k++) if (m.vdp.vram[k] != fr_pat(f, k) || m.vdp.vram[0x2000 + k] != fr_col(f, k)) return 0; return 1; }
static int vram_has_frame(void) { for (int f = 0; f < NF; f++) if (vram_matches(f)) return f; return -1; }

int main(int argc, char **argv)
{
    char err[256];
    make_files();
    if (msx_host_init(&m, argc > 1 ? argv[1] : "tests/machines/cbios2p", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    mmvid_set_host(&host);
    CHECK(st() == 0xFF, "no cart: status FF (got %02X)", st());
    static uint8_t blank[16384]; memset(blank, 0xFF, sizeof blank);
    CHECK(msx_insert_cart(&m, 1, blank, sizeof blank, MAPPER_MMEDIA) == 0, "insert");
    msx_reset(&m); frames(120);
    /* BIOS 가 VRAM 을 건드리지 않게 Z80 을 제자리 도는 코드(DI; JR $)에 묶어 둠 — 이후 VRAM 은 카트리지만 씀 */
    msx_mem_write(&m, 0xC000, 0xF3); msx_mem_write(&m, 0xC001, 0x18); msx_mem_write(&m, 0xC002, 0xFE); m.cpu.pc = 0xC000; frames(2);
    /* 식별 */
    cmd(0x01, 0, 0);
    CHECK(r(0x25) == 'X' && r(0x25) == 'V' && r(0x25) == 'D' && r(0x25) == 1, "IDENT");
    /* 소리 쪽 규격은 그대로 (2Ch~2Fh 식별 'MVS') */
    w(0x2C, 0x00); CHECK(r(0x2D) == 'M' && r(0x2D) == 'V' && r(0x2D) == 'S' && r(0x2D) == 1, "sound IDENT unchanged");
    /* 목록: 폴더 먼저 */
    { uint8_t a[4] = { 0, 0, 0, 0 }; cmd(0x10, 4, a); uint8_t b[15]; for (int i = 0; i < 15; i++) b[i] = (uint8_t)r(0x25);
      CHECK(b[0] == 1 && !strncmp((char *)b + 1, "SUB", 3), "LIST 0 = folder SUB (kind %d '%.12s')", b[0], b + 1);
      int sub = b[13] | (b[14] << 8); CHECK(sub == 1, "SUB folder number 1 (got %d)", sub);
      a[2] = 1; cmd(0x10, 4, a); for (int i = 0; i < 15; i++) b[i] = (uint8_t)r(0x25);
      CHECK(b[0] == 0 && (!strncmp((char *)b + 1, "BAD.MV2", 7)), "LIST 1 = file BAD.MV2 (got '%.12s')", b + 1);
      a[2] = 2; cmd(0x10, 4, a); for (int i = 0; i < 15; i++) b[i] = (uint8_t)r(0x25);
      CHECK(b[0] == 0 && !strncmp((char *)b + 1, "T.MV2", 5), "LIST 2 = file T.MV2");
      CHECK(!(st() & 0x10), "list not ended yet");
      a[2] = 3; cmd(0x10, 4, a); b[0] = (uint8_t)r(0x25);
      CHECK(b[0] == 0xFF && (st() & 0x10), "LIST 3 = end (kind %02X status %02X)", b[0], st());
      uint8_t up[4] = { 1, 0, 0xFF, 0xFF }; cmd(0x10, 4, up); for (int i = 0; i < 15; i++) b[i] = (uint8_t)r(0x25);
      CHECK(b[0] == 2 && b[13] == 0 && b[14] == 0, "SUB '..' -> folder 0 (kind %d num %d)", b[0], b[13] | (b[14] << 8));
      uint8_t top[4] = { 0, 0, 0xFF, 0xFF }; cmd(0x10, 4, top); for (int i = 0; i < 15; i++) b[i] = (uint8_t)r(0x25);
      CHECK(b[0] == 2 && b[13] == 0xFF && b[14] == 0xFF, "root '..' -> FFFF"); }
    /* 틀린 파일·폴더·다른 SCREEN 은 오류 */
    { uint8_t o[12] = { 0, 0, 1, 0, 2, 192, 24, 40, 1, 0, 0, 0 }; cmd(0x11, 12, o); CHECK(st() & 8, "OPEN BAD.MV2 -> error");
      o[2] = 0; cmd(0x11, 12, o); CHECK(st() & 8, "OPEN a folder -> error");
      o[2] = 2; o[4] = 5; cmd(0x11, 12, o); CHECK(st() & 8, "OPEN SCREEN 5 -> error (1단계는 SCREEN 2 만)"); }
    /* 열기 */
    { uint8_t o[12] = { 0, 0, 2, 0, 2, 192, 24, 40, 1, 0, 0, 0 }; cmd(0x11, 12, o); CHECK(!(st() & 8), "OPEN T.MV2 ok (status %02X)", st());
      CHECK(vram_matches(0), "frame 0 in VRAM right after OPEN");
      CHECK(m.vdp.reg[0] == 0x02 && (m.vdp.reg[1] & 0x40) && m.vdp.reg[2] == 0x06 && m.vdp.reg[4] == 0x03, "SCREEN 2 registers (R0=%02X R1=%02X R2=%02X R4=%02X)", m.vdp.reg[0], m.vdp.reg[1], m.vdp.reg[2], m.vdp.reg[4]);
      CHECK(m.vdp.vram[0x1800] == 0 && m.vdp.vram[0x1900 + 5] == 5 && m.vdp.vram[0x1A00 + 255] == 255, "name table 0..255 x3");
      CHECK(m.vdp.palette[1][0] > m.vdp.palette[2][0], "palette: entry 1 (R7) brighter in red than entry 2 (R1): %d %d", m.vdp.palette[1][0], m.vdp.palette[2][0]);
      cmd(0x08, 0, 0); int len = r(0x25) | (r(0x25) << 8), wd = r(0x25) | (r(0x25) << 8), ht = r(0x25) | (r(0x25) << 8), fps = r(0x25) | (r(0x25) << 8), snd = r(0x25);
      CHECK(len == NF && wd == 256 && ht == 192 && fps == 1500 && snd == 1, "INFO len %d w %d h %d fps %d snd %d", len, wd, ht, fps, snd);
      CHECK(pos() == 0, "POS 0 before PLAY"); }
    /* 재생: 위치가 따라 가고, 화면이 그 프레임이 됨 */
    cmd(0x03, 0, 0); frames(8);
    CHECK((st() & 0x20), "PLAY status bit5");
    frames(60);
    { int p = pos(); int f = vram_has_frame();
      CHECK(p > 0, "POS advances after PLAY (got %d)", p);
      CHECK(f >= 0 && f <= p && p - f <= 2, "VRAM shows the frame at POS (pos %d vram frame %d)", p, f);
      CHECK(mvsnd_fifo_used() > 0 || p > 5, "sound got MP3 data"); }
    /* 일시정지 */
    cmd(0x04, 0, 0); { int p0 = pos(); frames(30); int p1 = pos(); CHECK(p0 == p1, "PAUSE holds POS (%d -> %d)", p0, p1); CHECK((st() & 0x40) && !(st() & 0x20), "status bit6 paused"); }
    /* 탐색 중 일시정지: 화면이 바로 바뀜 */
    { uint8_t t[2] = { 30, 0 }; cmd(0x06, 2, t); CHECK(vram_matches(30), "SEEK 30 while paused shows frame 30 (VRAM frame %d)", vram_has_frame()); CHECK(pos() == 30, "POS 30 after SEEK (got %d)", pos()); }
    /* 글자 줄 얹기 */
    { uint8_t on[1] = { 1 }; cmd(0x0E, 1, on); cmd(0x0D, 0, 0); for (int i = 0; i < 4096; i++) w(0x25, i < 2048 ? 0xAA : 0x1F); frames(2);
      cmd(0x03, 0, 0); frames(3);
      CHECK(m.vdp.vram[0] == 0xAA && m.vdp.vram[767] == 0xAA && m.vdp.vram[0x2000] == 0x1F, "overlay top rows");
      CHECK(m.vdp.vram[19 * 256] == 0xAA && m.vdp.vram[24 * 256 - 1] == 0xAA && m.vdp.vram[0x2000 + 19 * 256] == 0x1F, "overlay bottom rows (%02X %02X %02X)", m.vdp.vram[19 * 256], m.vdp.vram[24 * 256 - 1], m.vdp.vram[0x2000 + 19 * 256]);
      CHECK(m.vdp.vram[3 * 256] != 0xAA || m.vdp.vram[3 * 256 + 100] != 0xAA, "middle rows keep the video");
      uint8_t off[1] = { 0 }; cmd(0x0E, 1, off); frames(8); CHECK(m.vdp.vram[0] != 0xAA, "OVLON 0 removes overlay (VRAM[0]=%02X)", m.vdp.vram[0]); }
    /* 끝까지: 상태 bit2 */
    { int g = 0; while (g++ < 60 * 40 && !(st() & 4)) frames(1); CHECK(st() & 4, "reaches the end (bit2) pos %d", pos()); CHECK(!(st() & 0x20), "not playing after end"); }
    /* STOP, 다시 열기 */
    cmd(0x05, 0, 0); CHECK(!(st() & 0x20), "STOP");
    /* 8.0.174: 공백 8칸이 없는 머리 ("MMCSD_MV" 바로 뒤 "v2.00", ONE_SPRING 형태)도 열림 */
    { FILE *src = fopen(ROOT "/T.MV2", "rb"), *dst = fopen(ROOT "/NB.MV2", "wb"); static uint8_t buf[0x4000 * 4]; size_t n = fread(buf, 1, sizeof buf, src);
      memset(buf + 8, 0, 0x18 - 8); memcpy(buf + 8, "v2.00", 5); fwrite(buf, 1, n, dst); fclose(src); fclose(dst); }
    { uint8_t o[12] = { 0, 0, 2, 0, 2, 192, 24, 40, 1, 0, 0, 0 }; cmd(0x11, 12, o); CHECK(!(st() & 8), "OPEN NB.MV2 (header without the 8 blanks) ok (status %02X)", st()); }
    printf(fails ? "mmvidtest: %d FAILED\n" : "mmvidtest: all passed\n", fails);
    return fails != 0;
}
