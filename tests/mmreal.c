/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* 실제 USB 의 곡·영상으로 X86 Multimedia USB 직접 접근을 끝까지 돌려 본다 (8.0.83 진단). 사용: mmreal <USB뿌리> [machine]
 * 뿌리/ 아래에 "Storage Device/mp3", "Storage Device/MV2" 가 있다고 보고, 1: = 뿌리. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../src/msx.h"
#include "../src/mmvid.h"
#include "../src/mmusb.h"
#include "../src/mvsnd.h"
static MSX m; static const char *ROOTD;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
static void hp(const char *p, char *o, size_t mx) { snprintf(o, mx, "%s%s", ROOTD, p[2] ? p + 2 : ""); for (char *c = o; *c; c++) if (*c == '\\') *c = '/'; }
static int h_vols(UsbVol *out, int max) { (void)max; snprintf(out[0].name, sizeof out[0].name, "Drive 1 USB"); snprintf(out[0].key, sizeof out[0].key, "1:"); out[0].boot = 0; return 1; }
static int h_list(const char *dir, UsbEnt *out, int max)
{ char d[900]; hp(dir, d, sizeof d); DIR *dd = opendir(d); if (!dd) return -1; struct dirent *e; int k = 0;
  while ((e = readdir(dd)) && k < max) { char p[1400]; snprintf(p, sizeof p, "%s/%s", d, e->d_name); struct stat st; if (stat(p, &st)) continue; if (strlen(e->d_name) >= sizeof out[k].name) continue;
    snprintf(out[k].name, sizeof out[k].name, "%s", e->d_name); out[k].is_dir = S_ISDIR(st.st_mode); out[k].size = (uint32_t)st.st_size; k++; }
  closedir(dd); return k; }
static int h_read(const char *path, uint32_t off, uint8_t *buf, uint32_t len)
{ char d[1200]; hp(path, d, sizeof d); FILE *f = fopen(d, "rb"); if (!f) return -1; fseek(f, (long)off, SEEK_SET); size_t n = fread(buf, 1, len, f); fclose(f); if (n < len) memset(buf + n, 0, len - n); return 0; }
static const UsbHost uhost = { h_vols, h_list, h_read, '\\' };
static const MmHost mh = { 0, h_read, "1:", '\\' };
static void w(int p, int v) { msx_io_write(&m, p, (uint8_t)v); }
static int  r(int p) { return msx_io_read(&m, p); }
static void vcmd(int c, int n, const uint8_t *a) { w(0x24, c); for (int i = 0; i < n; i++) w(0x26, a[i]); }
static void frames(int n) { for (int i = 0; i < n; i++) msx_run_frame(&m); }
static int  rd_list(uint8_t filt, char names[][130], int *isdir)
{ uint8_t a[4] = { 0, 0, 8, filt }; int n = 0, total = 0;
  for (int st = 0;; st += 8) { a[0] = (uint8_t)st; a[1] = (uint8_t)(st >> 8); vcmd(0x24, 4, a); total = r(0x25) | r(0x25) << 8; int c = r(0x25); r(0x25);
    for (int i = 0; i < c; i++) { isdir[n] = r(0x25); for (int k = 0; k < 4; k++) r(0x25); int l = r(0x25); for (int k = 0; k < l; k++) names[n][k] = (char)r(0x25); names[n][l] = 0; n++; }
    if (st + 8 >= total || n >= 64) break; }
  return n; }
static int find(char names[][130], int n, const char *s) { for (int i = 0; i < n; i++) if (strstr(names[i], s)) return i; return -1; }
int main(int argc, char **argv)
{
    char err[256]; ROOTD = argc > 1 ? argv[1] : "."; static char names[64][130]; int isdir[64];
    if (msx_host_init(&m, argc > 2 ? argv[2] : "tests/machines/cbios2p", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    static const MvFileOps fops = { 0, h_read, 0 };
    mmvid_set_host(&mh); mmusb_set_host(&uhost); mvsnd_set_file(&fops); mvsnd_set_usbfile(mmusb_cur_file);
    static uint8_t blank[16384]; memset(blank, 0xFF, sizeof blank);
    CHECK(msx_insert_cart(&m, 1, blank, sizeof blank, MAPPER_MMEDIA) == 0, "insert");
    msx_reset(&m); frames(120);
    msx_mem_write(&m, 0xC000, 0xF3); msx_mem_write(&m, 0xC001, 0x18); msx_mem_write(&m, 0xC002, 0xFE); m.cpu.pc = 0xC000; frames(2);
    vcmd(0x01, 0, 0); uint8_t id[5]; for (int i = 0; i < 5; i++) id[i] = (uint8_t)r(0x25); printf("IDENT caps %02x\n", id[4]);
    CHECK(id[4] & 4, "IDENT bit2 (글자 그리기) 없음");
    { /* 8.0.88 30h TEXT: 한글 제목 (UTF-8) */
        const char *u8 = "BoA \xEC\x95\x84\xEB\xA6\xAC\xEB\x9E\x91 (Spring Rain)"; int ln = (int)strlen(u8);
        uint8_t hd[5] = { 0, 9, 0, 0, (uint8_t)ln }; vcmd(0x30, 5, hd); for (int i = 0; i < ln; i++) w(0x26, (uint8_t)u8[i]);
        int st = r(0x24), used = r(0x25), drawn = r(0x25); int tw = r(0x25); tw |= r(0x25) << 8; int th = r(0x25);   /* (한 식 안에서 읽는 차례는 정해져 있지 않으니 따로)*/
        printf("TEXT used %d drawn %d width %d height %d status %02x\n", used, drawn, tw, th, st);
        CHECK(!(st & 8) && used == ln && drawn == 21 && th == 10 && tw > 100 && tw < 300, "TEXT 결과 이상");
    }
    uint8_t a1[1] = { 0 }; vcmd(0x22, 1, a1);
    int n = rd_list(0, names, isdir); int sd = find(names, n, "Storage Device"); CHECK(sd >= 0, "Storage Device 없음");
    uint8_t cd[2] = { (uint8_t)sd, 0 }; vcmd(0x23, 2, cd);
    /* ---- MP3 ---- */
    n = rd_list(0, names, isdir); int mp = find(names, n, "mp3"); CHECK(mp >= 0, "mp3 폴더 없음"); cd[0] = (uint8_t)mp; vcmd(0x23, 2, cd);
    n = rd_list(1, names, isdir); printf("mp3 폴더 %d개 항목\n", n);
    for (int t = 0; t < n; t++) {
        if (isdir[t]) continue;
        uint8_t o[2] = { (uint8_t)t, 0 }; vcmd(0x25, 2, o); uint32_t sz = r(0x25) | r(0x25) << 8 | r(0x25) << 16 | (uint32_t)r(0x25) << 24;
        w(0x2C, 2); w(0x2D, 0); w(0x2D, 0); w(0x2C, 0x0C);
        w(0x2C, 0x0A); uint8_t fi[16]; for (int i = 0; i < 16; i++) fi[i] = (uint8_t)r(0x2D);
        uint32_t len10 = fi[5] | fi[6] << 8 | fi[7] << 16;
        printf("[%s] %u bytes fmt %d rate %d len %u.%u s\n", names[t], sz, fi[0], fi[1] | fi[2] << 8, len10 / 10, len10 % 10);
        CHECK(fi[0] == 3, "형식 %d", fi[0]);
        w(0x2C, 0x02); long en = 0; int lastpos = 0;
        for (int fr = 0; fr < 60 * 20; fr++) { frames(1); if (fr % 60 == 0) { int16_t pcm[800]; mvsnd_render(pcm, 800); for (int k = 0; k < 800; k++) en += pcm[k] < 0 ? -pcm[k] : pcm[k]; } lastpos = (int)mvsnd_position(); }
        printf("   20초 재생 뒤 위치 %d 에너지 %ld, 재생비트 %d\n", lastpos, en, (r(0x2C) >> 3) & 1);
        w(0x2D, 0); w(0x2D, 0);
        w(0x2C, 0x00); /* seek 중간 */
        w(0x2E, 0); w(0x2F, 0); w(0x2C, 0x04);
        break;
    }
    /* ---- MV2 ---- */
    vcmd(0x23, 0, 0); { uint8_t up[2] = { 0xFF, 0xFF }; vcmd(0x23, 2, up); }
    n = rd_list(0, names, isdir); int mv = find(names, n, "MV2"); CHECK(mv >= 0, "MV2 폴더 없음"); cd[0] = (uint8_t)mv; vcmd(0x23, 2, cd);
    n = rd_list(2, names, isdir); printf("MV2 폴더 %d개 항목\n", n);
    for (int t = 0; t < n; t++) {
        if (isdir[t]) continue;
        uint8_t op[12] = { 0xFF, 0xFF, (uint8_t)t, 0, 2, 0, 0, 0, 0, 0, 0, 0 }; vcmd(0x11, 12, op);
        int st = r(0x24); printf("[%s] OPEN status %02x\n", names[t], st); CHECK(!(st & 8), "OPEN 오류");
        if (st & 8) continue;
        vcmd(0x08, 0, 0); int len = r(0x25) | r(0x25) << 8; printf("   프레임 %d\n", len);
        vcmd(0x03, 0, 0); frames(300);
        { const char *u8 = "\xED\x95\x9C\xEA\xB8\x80"; uint8_t hd[5] = { 0, 9, 0, 0, 6 }; vcmd(0x30, 5, hd); for (int i = 0; i < 6; i++) w(0x26, (uint8_t)u8[i]);   /* 8.0.88 재생 중 끼어들기 */
          int st = r(0x24); int u = r(0x25), d = r(0x25); int tw2 = r(0x25); tw2 |= r(0x25) << 8; printf("   재생 중 TEXT used %d drawn %d width %d status %02x\n", u, d, tw2, st);
          CHECK(!(st & 8) && (st & 0x20) && d == 2 && tw2 == 20, "재생 중 TEXT 이상 (상태 %02x)", st); }
        frames(300); vcmd(0x09, 0, 0); int pos = r(0x26) | r(0x27) << 8; printf("   10초 재생 뒤 위치 %d, 상태 %02x\n", pos, r(0x24));
        uint8_t sk[2] = { 0x00, 0x04 }; vcmd(0x07, 2, sk); frames(120); vcmd(0x09, 0, 0); printf("   SEEK 뒤 위치 %d\n", r(0x26) | r(0x27) << 8);
        vcmd(0x05, 0, 0);
        break;
    }
    printf(fails ? "mmreal: %d FAIL\n" : "mmreal: ok\n", fails); return fails ? 1 : 0;
}
