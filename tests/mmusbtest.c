/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86 Multimedia USB 직접 접근 (8.0.80) 시험: 가짜 USB 두 개(폴더)를 두고 포트 24h~27h 명령 20h~29h 를 Z80 처럼 두드린다. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../src/mmvid.h"
#include "../src/mmusb.h"
#include "../src/mvsnd.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
#define BASE "/tmp/mmusb_test"
static int two_vols = 1;

static void hp(const char *p, char *o, size_t m)      /* "1:/Music" → BASE/v1/Music */
{ snprintf(o, m, BASE "/v%c%s", p[0], p[2] ? p + 2 : ""); }
static int h_vols(UsbVol *out, int max)
{
    int n = two_vols ? 2 : 1; if (n > max) n = max;
    for (int i = 0; i < n; i++) { snprintf(out[i].name, sizeof out[i].name, i ? "Drive 2 MUSIC 28 GB" : "Drive 1 X86MSX 3 GB (boot)"); snprintf(out[i].key, sizeof out[i].key, "%d:", i); out[i].boot = i == 0; }
    return n;
}
static int g_nlist;
static int h_list(const char *dir, UsbEnt *out, int max)
{
    g_nlist++; char d[600]; hp(dir, d, sizeof d);
    DIR *dd = opendir(d); if (!dd) return -1;
    struct dirent *e; int k = 0;
    while ((e = readdir(dd)) && k < max) {
        char p[900]; snprintf(p, sizeof p, "%s/%s", d, e->d_name); struct stat st; if (stat(p, &st)) continue;
        snprintf(out[k].name, sizeof out[k].name, "%s", e->d_name); out[k].is_dir = S_ISDIR(st.st_mode) ? 1 : 0; out[k].size = (uint32_t)st.st_size; k++;
    }
    closedir(dd); return k;
}
static int h_read(const char *path, uint32_t off, uint8_t *buf, uint32_t len)
{
    char d[700]; hp(path, d, sizeof d);
    FILE *f = fopen(d, "rb"); if (!f) return -1;
    fseek(f, (long)off, SEEK_SET); size_t n = fread(buf, 1, len, f); fclose(f);
    if (n < len) memset(buf + n, 0, len - n);
    return 0;
}
/* 8.0.109 MSX 디스크 파일 흉내: 드라이브 2(C:) 의 MV\\T.MV5 만 있음 */
static int d_find(int dr, const char *p, char *h, int max, uint32_t *sz)
{
    if (dr == 3) { if (strcmp(p, "IMG.MV5")) return 0; h[0] = 1; h[1] = 'I'; h[2] = 0; *sz = 29696 * 3 + 0x4000; return 2; }   /* 하드 이미지 안 파일 흉내 (표식) */
    if (dr != 2) return -1; if (strcmp(p, "MV\\T.MV5")) return 0;
    snprintf(h, (size_t)max, "%s", "1:/MV/T.MV5"); char rp[600]; hp(h, rp, sizeof rp); struct stat st; if (stat(rp, &st)) return 0; *sz = (uint32_t)st.st_size; return 1;
}
static int d_read(const char *tok, uint32_t off, uint8_t *b, uint32_t n)
{
    (void)tok; FILE *f = fopen(BASE "/v1/MV/T.MV5", "rb"); if (!f) return -1;
    int r = fseek(f, (long)off, SEEK_SET) || fread(b, 1, n, f) != n ? -1 : 0; fclose(f); return r;
}
static const MmDisk disk_ops = { d_find, d_read };
static const UsbHost host = { h_vols, h_list, h_read, '/' };

/* Z80 처럼: 명령 → 인자 → 결과 */
static uint8_t cmd(uint8_t c, const uint8_t *a, int na) { mmvid_write(0, c); for (int i = 0; i < na; i++) mmvid_write(2, a[i]); return mmvid_read(0); }
static int  res(uint8_t *b, int n) { for (int i = 0; i < n; i++) b[i] = mmvid_read(1); return n; }
#define ST_END 4
#define ST_ERR 8
#define ST_LEND 16

static void mk(const char *p, const char *text) { mmusb_flush(); char c[900]; snprintf(c, sizeof c, "mkdir -p \"$(dirname '%s')\"", p); system(c); FILE *f = fopen(p, "wb"); fputs(text, f); fclose(f); }
static int list(uint16_t st, int n, uint8_t f, uint8_t *r)   /* 결과 전체 길이 */
{
    uint8_t a[4] = { (uint8_t)st, (uint8_t)(st >> 8), (uint8_t)n, f };
    uint8_t s = cmd(0x24, a, 4);
    r[0] = mmvid_read(1); r[1] = mmvid_read(1); r[2] = mmvid_read(1); r[3] = mmvid_read(1);
    int len = 4;
    for (int i = 0; i < r[2]; i++) { r[len++] = mmvid_read(1); for (int k = 0; k < 4; k++) r[len++] = mmvid_read(1); int nl = r[len++] = mmvid_read(1); for (int k = 0; k < nl; k++) r[len++] = mmvid_read(1); }
    return s | (len << 8);
}
static const char *nm(const uint8_t *r, int idx, int *isdir)      /* idx 번째 항목 이름 */
{
    static char b[200]; int p = 4;
    for (int i = 0; i < idx; i++) p += 6 + r[p + 5];
    if (isdir) *isdir = r[p];
    memcpy(b, r + p + 6, r[p + 5]); b[r[p + 5]] = 0; return b;
}

int main(void)
{
    mmusb_flush(); system("rm -rf " BASE "; mkdir -p " BASE "/v0/Storage\\ Device " BASE "/v1/Music/Rock " BASE "/v1/Video");
    mk(BASE "/v1/b.mp3", "BBBB"); mk(BASE "/v1/A.WAV", "AAAA"); mk(BASE "/v1/z.mv2", "ZZ"); mk(BASE "/v1/notes.txt", "NN");
    mk(BASE "/v1/Music/Rock/song.flac", "FLACDATA");
    const char *lng = "아주 긴 한글 이름의 음악 파일 이름입니다 아주 긴 한글 이름의 음악 파일 이름입니다 아주 긴 한글 이름의 음악 파일 이름입니다 끝.mp3";   /* 100 바이트가 넘음 */
    { char p[900]; snprintf(p, sizeof p, BASE "/v1/Music/%s", lng); mk(p, "LONG"); }
    static char big[2600]; for (int i = 0; i < 2600; i++) big[i] = (char)('a' + i % 26);
    { FILE *f = fopen(BASE "/v1/Music/big.mp3", "wb"); fwrite(big, 1, sizeof big, f); fclose(f); }
    mmusb_set_host(&host); mmvid_reset();

    uint8_t r[2048];
    /* 0 IDENT 의 5번째 바이트 */
    cmd(0x01, 0, 0); res(r, 5); CHECK(r[0] == 'X' && r[1] == 'V' && r[2] == 'D' && r[3] == 1 && (r[4] & 1), "IDENT 능력 비트 %02x", r[4]);
    /* 1 CHANGE · VOLS */
    uint8_t s = cmd(0x20, 0, 0); res(r, 2); uint8_t gen0 = r[0]; CHECK(!(s & ST_ERR) && r[1] == 2, "CHANGE nv=%d", r[1]);
    uint8_t a1[1] = { 0 }; s = cmd(0x21, a1, 1); res(r, 4); CHECK(r[0] == 0 && r[1] == 0 && r[2] == 1 && r[3] > 0, "VOLS0 %d %d %d", r[0], r[1], r[2]);
    { char t[80]; for (int i = 0; i < r[3]; i++) t[i] = (char)mmvid_read(1); t[r[3]] = 0; CHECK(!strcmp(t, "Drive 1 X86MSX 3 GB (boot)"), "VOLS0 이름 %s", t); }
    a1[0] = 1; s = cmd(0x21, a1, 1); res(r, 4); CHECK(r[0] == 0 && r[1] == 1 && r[2] == 0, "VOLS1");
    { for (int i = 0; i < r[3]; i++) mmvid_read(1); }
    a1[0] = 2; s = cmd(0x21, a1, 1); res(r, 1); CHECK(r[0] == 0xFF && (s & ST_LEND), "VOLS 끝");
    cmd(0x20, 0, 0); res(r, 2); CHECK(r[0] == gen0, "바뀜 없는데 번호 바뀜");
    /* 2 SEL 과 LIST */
    a1[0] = 5; s = cmd(0x22, a1, 1); CHECK(s & ST_ERR, "없는 USB 선택이 오류가 아님");
    s = list(0, 8, 0, r); CHECK((s & ST_ERR), "USB 안 고르고 LIST 가 오류가 아님");
    a1[0] = 1; s = cmd(0x22, a1, 1); CHECK(!(s & ST_ERR), "SEL");
    s = list(0, 8, 0, r); int total = r[0] | r[1] << 8; CHECK(total == 6 && r[2] == 6, "전체 %d 개 %d", total, r[2]);
    { int d; CHECK(!strcmp(nm(r, 0, &d), "Music") && d, "첫 항목 폴더 Music (%s)", nm(r, 0, &d)); CHECK(!strcmp(nm(r, 1, &d), "Video") && d, "둘째 Video"); CHECK(!strcmp(nm(r, 2, &d), "A.WAV") && !d, "A.WAV %s", nm(r, 2, &d)); CHECK(!strcmp(nm(r, 3, &d), "b.mp3"), "대소문자 무시 정렬 %s", nm(r, 3, &d)); }
    s = list(0, 8, 1, r); total = r[0] | r[1] << 8; CHECK(total == 4, "소리만 거르면 폴더 2 + wav·mp3 = 4 (%d)", total);
    s = list(0, 8, 2, r); total = r[0] | r[1] << 8; CHECK(total == 3, "영상만 = 폴더 2 + mv2 (%d)", total);
    mk(BASE "/v1/clip.MP4", "M"); mk(BASE "/v1/clip2.m4v", "M"); mk(BASE "/v1/clip3.mov", "M");
    s = list(0, 8, 2, r); total = r[0] | r[1] << 8; CHECK(total == 3, "8.0.90 걸러내기 2 는 mp4 를 안 보임 (%d)", total);
    s = list(0, 8, 8, r); total = r[0] | r[1] << 8; CHECK(total == 2 + 1 + 3, "8.0.90 걸러내기 8 = 폴더 2 + mv2 + mp4·m4v·mov (%d)", total);
    s = list(0, 8, 4, r); total = r[0] | r[1] << 8; CHECK(total == 3 + 3, "나머지(4)는 mp4 도 포함 (%d)", total);
    s = list(0, 8, 12, r); total = r[0] | r[1] << 8; CHECK(total == 2 + 1 + 1 + 3, "나머지+영상(12) = 폴더 2 + txt + mv2 + mp4 3 (%d)", total);
    mmusb_flush(); system("rm -f " BASE "/v1/clip.MP4 " BASE "/v1/clip2.m4v " BASE "/v1/clip3.mov");
    s = list(0, 8, 0, r);
    s = list(0, 8, 4, r); total = r[0] | r[1] << 8; CHECK(total == 3, "나머지 = 폴더 2 + txt (%d)", total);
    s = list(4, 8, 0, r); CHECK(r[2] == 2 && !(s & ST_LEND), "쪽 넘김 끝 2개 (%d)", r[2]);
    s = list(6, 8, 0, r); CHECK(r[2] == 0 && (s & ST_LEND), "시작이 끝이면 목록 끝");
    /* 3 CD */
    s = list(0, 8, 0, r);
    uint8_t cd[2] = { 2, 0 }; s = cmd(0x23, cd, 2); CHECK(s & ST_ERR, "파일로 CD 가 오류가 아님");
    cd[0] = 0; s = cmd(0x23, cd, 2); CHECK(!(s & ST_ERR), "Music 으로 CD");
    s = list(0, 8, 1, r); total = r[0] | r[1] << 8; CHECK(total == 3, "Music 안: Rock 폴더 + 긴 이름 mp3 + big.mp3 = 3 (%d)", total);
    { int d; const char *n3 = nm(r, 0, &d); CHECK(!strcmp(n3, "Rock") && d, "Music 첫 항목 Rock (%s)", n3); }
    { int p = 4; for (int i = 0; i < total; i++) { int l = r[p + 5]; CHECK(l <= 100, "이름이 100 바이트를 넘음 %d", l); p += 6 + l; } }
    /* 4 OPEN · READ · SEEK */
    { int d, big_i = -1; for (int i = 0; i < total; i++) if (!strcmp(nm(r, i, &d), "big.mp3")) big_i = i;
      CHECK(big_i >= 0, "big.mp3 못 찾음");
      uint8_t o[2] = { (uint8_t)big_i, 0 }; s = cmd(0x25, o, 2); res(r, 4); CHECK(!(s & ST_ERR) && (r[0] | r[1] << 8) == 2600, "OPEN 크기 %d", r[0] | r[1] << 8);
      uint8_t rd[2] = { 0, 4 }; static uint8_t got[2600]; int n = 0;       /* 1024 씩 */
      for (int k = 0; k < 4 && n < 2600; k++) { s = cmd(0x26, rd, 2); int c = 2600 - n > 1024 ? 1024 : 2600 - n; for (int i = 0; i < c; i++) got[n++] = mmvid_read(1); if (n < 2600) CHECK(!(s & ST_END), "중간에 끝"); }
      CHECK(n == 2600 && !memcmp(got, big, 2600) && (s & ST_END), "READ 내용·끝 비트 (%d)", n);
      s = cmd(0x26, rd, 2); CHECK(s & ST_END, "끝 뒤 READ 는 끝");
      uint8_t sk[4] = { 100, 0, 0, 0 }; s = cmd(0x27, sk, 4); uint8_t r2[2] = { 10, 0 }; s = cmd(0x26, r2, 2); for (int i = 0; i < 10; i++) CHECK(mmvid_read(1) == (uint8_t)big[100 + i], "SEEK 뒤 읽기 %d", i);
      uint8_t sk2[4] = { 0xFF, 0xFF, 0xFF, 0x7F }; s = cmd(0x27, sk2, 4); s = cmd(0x26, r2, 2); CHECK(s & ST_END, "SEEK 크기 넘으면 잘려 끝");
      s = cmd(0x29, 0, 0); res(r, 2); CHECK(r[0] == 1 && r[1] == 1, "INFO 열림 %d %d", r[0], r[1]);
      cmd(0x28, 0, 0); s = cmd(0x26, rd, 2); CHECK(s & ST_ERR, "CLOSE 뒤 READ 는 오류");
    }
    /* 5 위로 */
    uint8_t up[2] = { 0xFF, 0xFF }; s = cmd(0x23, up, 2); CHECK(!(s & ST_ERR), "Music 에서 위로");
    s = cmd(0x23, up, 2); CHECK(s & ST_ERR, "맨 위에서 위로는 오류");
    s = cmd(0x29, 0, 0); res(r, 2); { char pth[20]; for (int i = 0; i < 20; i++) { r[2 + i] = 0; } /* 크기·위치 8바이트 */ for (int i = 0; i < 8; i++) mmvid_read(1); for (int i = 0; i < 19; i++) { pth[i] = (char)mmvid_read(1); if (!pth[i]) break; pth[19] = 0; } CHECK(!strcmp(pth, "1:"), "INFO 경로 %s", pth); }
    /* 6 시스템 USB 도 읽기만으로 보임 */
    a1[0] = 0; s = cmd(0x22, a1, 1); s = list(0, 8, 0, r); CHECK(!(s & ST_ERR), "시스템 USB 목록");
    /* 7 항목이 너무 많은 폴더 */
    a1[0] = 1; cmd(0x22, a1, 1);
    mmusb_flush(); system("mkdir -p " BASE "/v1/Many; cd " BASE "/v1/Many && for i in $(seq 1 4300); do : > f$i.mp3; done");
    s = list(0, 8, 0, r); int mi = -1; { int d; for (int i = 0; i < 6; i++) if (!strcmp(nm(r, i, &d), "Many")) mi = i; }
    cd[0] = (uint8_t)mi; cd[1] = 0; cmd(0x23, cd, 2); s = list(0, 8, 0, r); total = r[0] | r[1] << 8; CHECK(total >= 4000 && (r[3] & 1), "너무 많으면 앞 4096개 + 표시 (%d, flag %d)", total, r[3]);
    s = list(4090, 8, 0, r); CHECK(r[2] > 0, "큰 목록 끝쪽 읽기");
    /* 8 USB 를 뽑으면 */
    two_vols = 0; cmd(0x20, 0, 0); res(r, 2); CHECK(r[0] != gen0 && r[1] == 1, "뽑으니 바뀜 번호 +1, 수 1 (%d, %d)", r[0], r[1]);
    s = list(0, 8, 0, r); CHECK(s & ST_ERR, "고른 USB 가 없어진 뒤 LIST 가 오류가 아님");
    two_vols = 1; cmd(0x20, 0, 0); res(r, 2); s = list(0, 8, 0, r); CHECK(s & ST_ERR, "다시 꽂아도 선택은 해제된 채");
    /* 8 MP3 직접 재생 (8.0.81): USB 에서 OPEN 한 파일을 0Ch 로 재생 */
    {
        static const MvFileOps fops = { 0, h_read, 0 };
        mvsnd_set_file(&fops); mvsnd_set_usbfile(mmusb_cur_file); mvsnd_reset();
        mmusb_flush(); system("cp tests/data/mvsnd_test.mp3 " BASE "/v1/Music/t.mp3");
        a1[0] = 1; cmd(0x22, a1, 1); s = list(0, 8, 0, r);
        int d, mi2 = -1; for (int i = 0; i < r[2]; i++) if (!strcmp(nm(r, i, &d), "Music") && d) mi2 = i;
        cd[0] = (uint8_t)mi2; cd[1] = 0; cmd(0x23, cd, 2); s = list(0, 8, 1, r); total = r[0] | r[1] << 8;
        int ti = -1; for (int i = 0; i < r[2]; i++) if (!strcmp(nm(r, i, &d), "t.mp3")) ti = i;
        CHECK(ti >= 0, "t.mp3 못 찾음 (%d개)", total);
        uint8_t o[2] = { (uint8_t)ti, 0 }; s = cmd(0x25, o, 2); res(r, 4);
        mvsnd_write(0, 0x00); for (int i = 0; i < 6; i++) mvsnd_read(1);
        mvsnd_write(0, 0x00); uint8_t id[6]; for (int i = 0; i < 6; i++) id[i] = mvsnd_read(1);
        CHECK(id[0] == 'M' && (id[4] & 0x04), "IDENT 5번째 bit2 (USB MP3) %02x", id[4]);
        mvsnd_write(2, 0); mvsnd_write(3, 0); mvsnd_write(0, 0x0C);
        mvsnd_write(0, 0x0A); uint8_t fi[16]; for (int i = 0; i < 16; i++) fi[i] = mvsnd_read(1);
        uint32_t len10 = fi[5] | fi[6] << 8 | fi[7] << 16;
        CHECK(fi[0] == 3 && (fi[1] | fi[2] << 8) > 8000 && len10 > 10, "FILE_INFO 형식 %d 표본 %d 길이 %u (0.1초)", fi[0], fi[1] | fi[2] << 8, len10);
        uint32_t fb = mvsnd_fifo_used(); mvsnd_write(1, 0x55); mvsnd_write(1, 0x66); CHECK(mvsnd_fifo_used() == fb, "2Dh 바이트가 무시되지 않음");
        mvsnd_write(0, 0x02);
        long energy = 0; int ended = 0, i;
        for (i = 0; i < 400; i++) { int16_t pcm[800]; mvsnd_render(pcm, 800); for (int k = 0; k < 800; k++) energy += pcm[k] < 0 ? -pcm[k] : pcm[k]; if (!(mvsnd_read(0) & 0x08)) { ended = 1; break; } }
        CHECK(energy > 100000, "소리가 안 남 (에너지 %ld)", energy);
        printf("mp3 직접: 길이 %u.%u초, 에너지 %ld, %d 번째 렌더(0.5초 단위 아님, 800샘플)에서 끝\n", len10 / 10, len10 % 10, energy, i);
        CHECK(ended, "파일 끝에서 재생 비트가 안 꺼짐 (%d 번 렌더)", i);
        CHECK(mvsnd_position() > 15, "위치가 안 감 %u", mvsnd_position());
        mvsnd_write(2, 0); mvsnd_write(3, 0); mvsnd_write(0, 0x0C); mvsnd_write(0, 0x02);
        mvsnd_write(2, 10); mvsnd_write(3, 0); mvsnd_write(0, 0x0B);                      /* 1.0초로 */
        CHECK(mvsnd_position() == 15, "SEEK 뒤 위치 %u (15 여야)", mvsnd_position());
        energy = 0; for (i = 0; i < 20; i++) { int16_t pcm[800]; mvsnd_render(pcm, 800); for (int k = 0; k < 800; k++) energy += pcm[k] < 0 ? -pcm[k] : pcm[k]; }
        CHECK(energy > 10000, "SEEK 뒤 소리가 안 남 (%ld)", energy);
        mvsnd_write(0, 0x04); mvsnd_write(1, 0x11); CHECK(mvsnd_fifo_used() == 1, "STOP 뒤에는 2Dh 로 받음");
        mvsnd_write(0, 0x01); mvsnd_write(0, 0x04);
        cmd(0x28, 0, 0); mvsnd_write(2, 0); mvsnd_write(3, 0); mvsnd_write(0, 0x0C); mvsnd_write(0, 0x0A); { uint8_t f0 = mvsnd_read(1); mvsnd_write(0, 0x02); CHECK(f0 == 0 && !(mvsnd_read(0) & 0x08), "파일 안 열고 0Ch: 형식 %d, 재생 비트 %d", f0, mvsnd_read(0) & 8); }
        mvsnd_set_usbfile(0); mvsnd_write(0, 0x0C); mvsnd_write(0, 0x00); for (i = 0; i < 6; i++) id[i] = mvsnd_read(1); CHECK(!(id[4] & 4), "연결 없으면 능력 비트 0");
    }
    /* 8.0.82 USB 의 MV2 재생: 영상 OPEN P0P1=FFFFh */
    {
        static const MmHost mh = { 0, h_read, BASE "/Video", '/' };
        mmvid_set_host(&mh); mmusb_set_host(&host); mmvid_reset();
        mmusb_flush(); system("mkdir -p " BASE "/v1/Video/Sub; python3 - <<'PY'\nf=open('" BASE "/v1/Video/Z.mv2','wb')\nh=bytearray(0x4000); h[0:8]=b'MMCSD_MV'; h[0x10:0x15]=b'v2.00'; h[0x18]=3\nf.write(h)\nfor i in range(3): f.write(bytes(0x4000))\nf.close()\nPY");
        mmusb_flush(); system("python3 -c \"open('" BASE "/v1/Video/A.avi','wb').write(b'RIFF'+bytes(4)+b'AVI LIST'+bytes(64))\"");   /* 8.0.117 AVI 는 목록에 보이고 OPEN 은 오류 11 */
        cmd(0x01, 0, 0); res(r, 5); CHECK((r[4] & 3) == 3, "IDENT 5번째 bit0·bit1 %02x", r[4]);
        a1[0] = 1; cmd(0x22, a1, 1); s = list(0, 8, 0, r);
        int d, vi = -1; for (int i = 0; i < r[2]; i++) if (!strcmp(nm(r, i, &d), "Video") && d) vi = i;
        cd[0] = (uint8_t)vi; cd[1] = 0; cmd(0x23, cd, 2); s = list(0, 8, 2, r);
        int zi = -1; for (int i = 0; i < r[2]; i++) if (!strcmp(nm(r, i, &d), "Z.mv2")) zi = i;
        CHECK(zi >= 0, "Z.mv2 목록에 없음");
        uint8_t op[12] = { 0xFF, 0xFF, (uint8_t)zi, 0, 2, 0, 0, 0, 0, 0, 0, 0 };
        s = cmd(0x11, op, 12); CHECK(!(s & ST_ERR), "USB 영상 OPEN 오류");
        cmd(0x08, 0, 0); res(r, 2); CHECK((r[0] | r[1] << 8) == 3, "영상 프레임 수 %d", r[0] | r[1] << 8);
        s = cmd(0x29, 0, 0); res(r, 2); CHECK(r[0] == 1 && r[1] == 0, "영상 OPEN 뒤에도 고른 USB·폴더 유지 (USB %d, 파일열림 %d)", r[0], r[1]);
        op[2] = 9; op[3] = 0; s = cmd(0x11, op, 12); CHECK(s & ST_ERR, "없는 번호 OPEN 이 오류가 아님");
        s = list(0, 8, 2, r); int si = -1; for (int i = 0; i < r[2]; i++) if (!strcmp(nm(r, i, &d), "Sub") && d) si = i;
        op[2] = (uint8_t)si; s = cmd(0x11, op, 12); CHECK(s & ST_ERR, "폴더로 OPEN 이 오류가 아님");
        op[0] = 0; op[1] = 0; op[2] = 0; s = cmd(0x11, op, 12); CHECK(s & ST_ERR, "기존 방식(P0P1 != FFFF)은 영상 폴더 표 기준이라 LIST 없이는 오류");
        s = list(0, 8, 8, r); { int ai = -1; for (int i = 0; i < r[2]; i++) if (!strcmp(nm(r, i, &d), "A.avi")) ai = i; CHECK(ai >= 0, "A.avi 가 영상 목록에 없음");
          op[0] = 0xFF; op[1] = 0xFF; op[2] = (uint8_t)ai; op[3] = 0; op[4] = 12; s = cmd(0x11, op, 12); CHECK(s & ST_ERR, "AVI OPEN 이 오류가 아님"); res(r, 1); CHECK(r[0] != 0, "AVI OPEN 오류 코드 %d (망가진 AVI)", r[0]); }
    }
    /* 8.0.101 USB 의 MV5~MVC: 필터 bit4, IDENT bit4, OPEN 11h 형식·오류 (4 = 머리 글자와 P4 다름, 6 = VDP·VRAM 부족) */
    {
        static VDP vd;
        static const char fc[3] = { '5', '8', 'C' };      /* 8.0.149: MV6·MV7·MVA·MVB 는 없앰 (MV8 은 8.0.150 에 되살림) */
        static const int fs[3] = { 5, 8, 12 };
        static const unsigned fz[3] = { 29696, 54272, 54272 }, fpic[3] = { 24576, 49152, 49152 }, fpal[3] = { 0x6000, 0, 0 };
        static const uint8_t fr0[3] = { 0x06, 0x0E, 0x0E }, fr25[3] = { 0, 0, 0x08 };
        static const MmHost mh = { 0, h_read, BASE "/Video", '/' };
        mmvid_set_host(&mh); mmusb_set_host(&host); mmvid_reset(); mmvid_attach(&vd);
        mmusb_flush(); system("mkdir -p " BASE "/v1/MV; python3 - <<'PY'\nimport struct\nfor c,fz,pic,pal,snd in [('5',29696,24576,0x6000,0x6200),('8',54272,49152,0,0xC200),('C',54272,49152,0,0xC200)]:\n  f=open('" BASE "/v1/MV/T.MV'+c,'wb'); h=bytearray(0x4000); h[0:8]=b'MMCSD_MV'; h[0x11]=ord(c); h[0x18]=3; h[0x20]=3; h[0x21]=192; f.write(h)\n  for i in range(3):\n    fr=bytearray(fz); fr[0:pic]=bytes([i+1])*pic\n    if pal:\n      for k in range(16): fr[pal+2*k]=(k&7)<<4|((k>>1)&7); fr[pal+2*k+1]=(k+1)&7\n    fr[snd]=0\n    f.write(fr)\n  f.close()\nopen('" BASE "/v1/MV/x.txt','wb').write(b'x')\nPY");
        cmd(0x01, 0, 0); res(r, 5); CHECK((r[4] & 0x10) == 0x10 && (r[4] & 3) == 3, "IDENT bit4 (USB MV5~MVC) %02x", r[4]);
        a1[0] = 1; cmd(0x22, a1, 1); s = list(0, 8, 0, r);
        int d, vi = -1; for (int i = 0; i < r[2]; i++) if (!strcmp(nm(r, i, &d), "MV") && d) vi = i;
        CHECK(vi >= 0, "MV 폴더 없음");
        cd[0] = (uint8_t)vi; cd[1] = 0; cmd(0x23, cd, 2);
        s = list(0, 8, 16, r); { int n = 0; for (int i = 0; i < r[2]; i++) { nm(r, i, &d); if (!d) n++; } CHECK(n == 3, "필터 16 = MV5·MV8·MVC 3개 (%d)", n); }
        s = list(0, 8, 24, r); { int n = 0; for (int i = 0; i < r[2]; i++) { nm(r, i, &d); if (!d) n++; } CHECK(n == 3, "필터 24 = 영상 + MV 3개 (%d)", n); }
        s = list(0, 8, 8, r); { int n = 0; for (int i = 0; i < r[2]; i++) { nm(r, i, &d); if (!d) n++; } CHECK(n == 0, "필터 8 에는 MV5~MVC 가 안 나옴 (%d)", n); }
        s = list(0, 8, 4, r); { int n = 0; for (int i = 0; i < r[2]; i++) { nm(r, i, &d); if (!d) n++; } CHECK(n == 4, "필터 4(나머지)에는 MV 3개 + txt (%d)", n); }
        s = list(0, 8, 16, r);
        int idx[3]; for (int k = 0; k < 3; k++) { char nn[16]; snprintf(nn, sizeof nn, "T.MV%c", fc[k]); idx[k] = -1; for (int i = 0; i < r[2]; i++) if (!strcmp(nm(r, i, &d), nn)) idx[k] = i; CHECK(idx[k] >= 0, "%s 목록에 없음", nn); }
        for (int k = 0; k < 3; k++) {
            vdp_init(&vd, 3); vdp_reset(&vd);                                 /* V9968: 모든 화면 가능 */
            uint8_t op[12] = { 0xFF, 0xFF, (uint8_t)idx[k], 0, (uint8_t)fs[k], 0, 0, 0, 0, 0, 0, 0 };
            s = cmd(0x11, op, 12); CHECK(!(s & ST_ERR), "MV%c OPEN 오류", fc[k]);
            cmd(0x08, 0, 0); res(r, 9); CHECK((r[0] | r[1] << 8) == 9 && (r[2] | r[3] << 8) == (fs[k] == 6 || fs[k] == 7 ? 512 : 256) && (r[4] | r[5] << 8) == 192 && (r[6] | r[7] << 8) == 500, "MV%c INFO 길이 %d 가로 %d 세로 %d fps100 %d", fc[k], r[0] | r[1] << 8, r[2] | r[3] << 8, r[4] | r[5] << 8, r[6] | r[7] << 8);
            CHECK(vd.reg[0] == fr0[k] && vd.reg[25] == fr25[k] && (vd.reg[1] & 0x40), "MV%c 레지스터 R0 %02X R25 %02X R1 %02X", fc[k], vd.reg[0], vd.reg[25], vd.reg[1]);
            if (fpal[k]) { uint8_t pr, pg, pb; (void)pr; (void)pg; (void)pb; CHECK(vd.palette[1][0] != 0 || vd.palette[1][1] != 0 || vd.palette[1][2] != 0, "MV%c 팔레트 적용", fc[k]); }
            { uint8_t ph[2 * 131072]; vdp_vram_physical(&vd, ph); int ok = 1; if (!(fs[k] == 6 || fs[k] == 7 || fs[k] >= 8)) { for (unsigned i = 0; i < 100; i++) if (ph[i] != 1) ok = 0; CHECK(ok, "MV%c 첫 프레임 그림이 VRAM 에", fc[k]); } }
            { const uint8_t *phb = vd.vram; int rb = (fs[k] == 5 || fs[k] == 6) ? 128 : 256, diff = 0, outside = 1;   /* 8.0.156 옛 SCREEN n 번호표는 없어지고(표시줄 안의 이름표가 대신함) 그림은 그대로, 화면은 212줄 */
              for (int y = 26; y < 40; y++) for (int x = 1; x < rb; x++) if (phb[y * rb + x] != phb[y * rb]) diff = 1;
              for (int y = 60; y < 100; y++) for (int x = 1; x < rb; x++) if (phb[y * rb + x] != phb[y * rb]) outside = 0;
              CHECK(!diff, "MV%c 옛 번호표가 없음 (26~39줄은 그림 그대로)", fc[k]); CHECK(outside, "MV%c 번호표 밖 그림은 그대로", fc[k]); CHECK(vd.reg[9] & 0x80, "MV%c 212줄 (표시줄 자리) R9 %02X", fc[k], vd.reg[9]); }
            { static uint8_t ovd[4096], ovl[4096]; memset(ovd, 0, sizeof ovd); memset(ovl, 0, sizeof ovl);   /* 8.0.143 글자 줄(OVL)이 MV5~MVC 에서도 얹히는지: 전부 어두운색 / 전부 밝은색으로 칠해 두 결과가 다른지 */
              for (int i = 0; i < 2048; i++) ovl[i] = 0xFF; for (int i = 2048; i < 4096; i++) ovl[i] = 0x10;
              static uint8_t ph[2 * 131072]; int rb = (fs[k] == 5 || fs[k] == 6) ? 128 : 256; uint8_t px[2];
              for (int w2 = 0; w2 < 2; w2++) {
                  const uint8_t *ov = w2 ? ovl : ovd;
                  cmd(0x0D, 0, 0); for (int i = 0; i < 4096; i++) mmvid_write(1, ov[i]); { uint8_t on1[1] = { 1 }; cmd(0x0E, on1, 1); } mmvid_tick(&vd);
                  vdp_vram_physical(&vd, ph); px[w2] = ph[0]; int top = 1, bot = 1; int msk = fs[k] >= 10 ? 0xF8 : 0xFF; for (int x = 1; x < rb; x++) { if ((ph[x] & msk) != (ph[0] & msk)) top = 0; if ((ph[191 * rb + x] & msk) != (ph[191 * rb] & msk)) bot = 0; }
                  CHECK(top && bot, "MV%c 글자 줄 %s: 위·아래 줄이 한 색 (top %d bot %d)", fc[k], w2 ? "밝음" : "어둠", top, bot);
              }
              CHECK(px[0] == px[1], "MV%c 8.0.161 파란 표시줄이 있으면 옛 글자 줄은 안 얹힘: 어둠 %02X 밝음 %02X", fc[k], px[0], px[1]);
              { int nc = 0; uint8_t seen[256] = { 0 }; for (int y = 192; y < 212; y++) for (int x = 0; x < rb; x++) { uint8_t v2 = vd.vram[y * rb + x]; if (!seen[v2]) { seen[v2] = 1; nc++; } }
                CHECK(nc >= 3, "MV%c 글자 줄 켬: 192~211줄에 파란 표시줄 (색 %d가지)", fc[k], nc); }   /* 8.0.156 */
              { uint8_t off1[1] = { 0 }; cmd(0x0E, off1, 1); } mmvid_tick(&vd);
              { int nc = 0; uint8_t seen[256] = { 0 };  for (int y = 192; y < 212; y++) for (int x = 0; x < rb; x++) { uint8_t v2 = vd.vram[y * rb + x]; if (!seen[v2]) { seen[v2] = 1; nc++; } }
                CHECK(nc == 1, "MV%c 글자 줄 끔: 표시줄 자리는 한 색 (색 %d가지)", fc[k], nc); } }
            { uint8_t wr[12]; memcpy(wr, op, 12); wr[4] = (uint8_t)(fs[k] == 5 ? 6 : 5); s = cmd(0x11, wr, 12); CHECK(!(s & ST_ERR), "MV%c 에 다른 P4 가 와도 자기 화면으로 열림 (8.0.145)", fc[k]); cmd(0x05, 0, 0); }
        }
        /* 8.0.145 MV5~MVC 는 설정(P4)과 상관없이 자기 화면: MV5 를 P4 = 13(V9 FAST)·8·12·0 으로 열어도 SCREEN 5, MV8 을 P4 = 5 로 열면 SCREEN 8 */
        { static const uint8_t p4s[4] = { 13, 8, 12, 0 };
          for (int q = 0; q < 4; q++) { vdp_init(&vd, 3); vdp_reset(&vd); uint8_t op[12] = { 0xFF, 0xFF, (uint8_t)idx[0], 0, p4s[q], 0, 0, 0, 0, 0, 0, 0 }; s = cmd(0x11, op, 12);
              CHECK(!(s & ST_ERR) && vd.reg[0] == 0x06, "MV5 를 P4 %d 로 열어도 SCREEN 5 (오류 %d, R0 %02X)", p4s[q], !!(s & ST_ERR), vd.reg[0]); cmd(0x05, 0, 0); }
          vdp_init(&vd, 3); vdp_reset(&vd); uint8_t o8[12] = { 0xFF, 0xFF, (uint8_t)idx[1], 0, 5, 0, 0, 0, 0, 0, 0, 0 }; s = cmd(0x11, o8, 12);
          CHECK(!(s & ST_ERR) && vd.reg[0] == 0x0E, "MV8 을 P4 5 로 열어도 SCREEN 8 (R0 %02X)", vd.reg[0]); cmd(0x05, 0, 0); }
        /* V9938: SCREEN 10~12 오류 6 / VRAM 64KB: SCREEN 7·8·10~12 오류 6 */
        for (int k = 2; k < 3; k++) { vdp_init(&vd, 1); vdp_reset(&vd); uint8_t op[12] = { 0xFF, 0xFF, (uint8_t)idx[k], 0, (uint8_t)fs[k], 0, 0, 0, 0, 0, 0, 0 }; s = cmd(0x11, op, 12); res(r, 1); CHECK((s & ST_ERR) && r[0] == 6, "V9938 에서 MV%c 오류 6 (%d)", fc[k], r[0]); }
        for (int k = 1; k < 3; k++) { vdp_init(&vd, 2); vdp_reset(&vd); vd.vram_mask = 0xFFFF; uint8_t op[12] = { 0xFF, 0xFF, (uint8_t)idx[k], 0, (uint8_t)fs[k], 0, 0, 0, 0, 0, 0, 0 }; s = cmd(0x11, op, 12); res(r, 1); CHECK((s & ST_ERR) && r[0] == 6, "VRAM 64KB 에서 MV%c 오류 6 (%d)", fc[k], r[0]); }
        { vdp_init(&vd, 1); vdp_reset(&vd); uint8_t op[12] = { 0xFF, 0xFF, (uint8_t)idx[0], 0, 5, 0, 0, 0, 0, 0, 0, 0 }; s = cmd(0x11, op, 12); CHECK(!(s & ST_ERR), "V9938 에서 MV5 는 됨"); }
        /* 재생·POS·SEEK·STOP */
        { vdp_init(&vd, 3); vdp_reset(&vd); uint8_t op[12] = { 0xFF, 0xFF, (uint8_t)idx[0], 0, 5, 0, 0, 0, 0, 0, 0, 0 }; cmd(0x11, op, 12);
          cmd(0x03, 0, 0); mmvid_read(0); cmd(0x09, 0, 0); CHECK(mmvid_playing(), "MV5 PLAY 중");
          uint8_t sk[2] = { 6, 0 }; s = cmd(0x06, sk, 2); cmd(0x09, 0, 0); (void)s;                        /* 6 = 1/15초 단위 → 프레임 2 */
          cmd(0x05, 0, 0); CHECK(!mmvid_playing(), "MV5 STOP 뒤 재생 아님"); }
        /* 8.0.107 X86MV 실기 순서: 긴 이름 파일, LIST(start=번호,count=1,필터 24) → CHANGE → OPEN → INFO. 실패한 OPEN 뒤 PLAY/POS/SEEK/STOP 은 안전 */
        {
            mmusb_flush(); system("cp " BASE "/v1/MV/T.MV5 \"" BASE "/v1/MV/14035-254146872_medium.MV5\"");
            vdp_init(&vd, 3); vdp_reset(&vd);
            s = cmd(0x20, 0, 0); res(r, 2);
            s = list(0, 8, 24, r); int li = -1; for (int i = 0; i < r[2]; i++) if (!strcmp(nm(r, i, &d), "14035-254146872_medium.MV5")) li = i;
            CHECK(li >= 0, "긴 이름 MV5 가 목록에 있음");
            if (li >= 0) {
                s = list((uint16_t)li, 1, 24, r); CHECK(r[2] == 1 && !strcmp(nm(r, 0, &d), "14035-254146872_medium.MV5"), "start=번호 count=1 로 한 줄");
                cmd(0x20, 0, 0);                                                  /* CHANGE 한 번 더 (결과 안 읽음) */
                uint8_t op[12] = { 0xFF, 0xFF, (uint8_t)li, (uint8_t)(li >> 8), 5, 0, 0, 0, 0, 0, 0, 0 };
                s = cmd(0x11, op, 12); CHECK(!(s & ST_ERR), "X86MV 순서 OPEN 오류 (%02X)", s);
                cmd(0x08, 0, 0); res(r, 9); CHECK((r[0] | r[1] << 8) == 9, "X86MV 순서 INFO 길이 %d", r[0] | r[1] << 8);
                cmd(0x05, 0, 0);
            }
            /* 안 열린 상태(OPEN 실패)에서 PLAY / POS / SEEK / STOP 이 안전한지 */
            { uint8_t op[12] = { 0xFF, 0xFF, 200, 0, 5, 0, 0, 0, 0, 0, 0, 0 }; s = cmd(0x11, op, 12); CHECK(s & ST_ERR, "없는 번호 OPEN 은 bit3 로 알림");
              s = cmd(0x03, 0, 0); CHECK(!mmvid_playing(), "열리지 않은 영상 PLAY 는 재생이 아님");
              cmd(0x09, 0, 0); CHECK(mmvid_read(2) == 0 && mmvid_read(3) == 0, "열리지 않은 영상 POS 는 0");
              uint8_t sk[2] = { 6, 0 }; cmd(0x06, sk, 2); cmd(0x04, 0, 0); cmd(0x05, 0, 0); CHECK(!mmvid_playing(), "열리지 않은 영상 SEEK/PAUSE/STOP 안전"); }
        }
        printf("mv5~mvc: 3형식 열기·INFO·오류 4/6·재생 시험 완료\n");
    }
    /* 8.0.108 필터만 바뀌면 USB 폴더를 다시 읽지 않음 (3.3초 지연 방지) */
    {
        uint8_t rr[300];
        mmusb_flush(); list(0, 8, 24, rr); int n1 = g_nlist;
        list(0, 8, 2, rr); list(0, 8, 8, rr); list(0, 8, 24, rr);
        CHECK(g_nlist == n1, "필터만 바꾸면 호스트 목록을 다시 읽지 않음 (%d -> %d)", n1, g_nlist);
        mmusb_flush(); list(0, 8, 24, rr);
        CHECK(g_nlist == n1 + 1, "flush 뒤에는 다시 읽음");
    }
    /* 8.0.109 MSX 디스크 파일: 2Ah SETPATH(드라이브 + 경로 0 끝) → OPEN P0P1=FFFDh → INFO → PLAY */
    {
        static VDP vd; uint8_t rr[16], sp[40]; int n;
        mmvid_attach(&vd);
        mmvid_set_disk(0); mmvid_reset(); cmd(0x01, 0, 0); res(rr, 5); CHECK(!(rr[4] & 0x20), "디스크 장치 없으면 능력 bit5 = 0");
        mmvid_set_disk(&disk_ops); mmvid_reset(); cmd(0x01, 0, 0); res(rr, 5); CHECK(rr[4] & 0x20, "능력 bit5 = MSX 디스크 파일");
        vdp_init(&vd, 3); vdp_reset(&vd);
        n = 0; sp[n++] = 2; for (const char *q = "MV\\T.MV5"; *q; q++) sp[n++] = (uint8_t)*q; sp[n++] = 0;
        cmd(0x2A, sp, n);
        uint8_t op[12] = { 0xFD, 0xFF, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0 };
        s = cmd(0x11, op, 12); { uint8_t ec[1]; res(ec, 1); CHECK(!(s & ST_ERR), "SETPATH + OPEN FFFDh 오류 코드 %d", ec[0]); }
        cmd(0x08, 0, 0); res(rr, 9); CHECK((rr[0] | rr[1] << 8) == 9, "디스크 MV5 INFO 길이 %d", rr[0] | rr[1] << 8);
        cmd(0x03, 0, 0); mmvid_read(0); CHECK(mmvid_playing(), "디스크 영상 PLAY 중"); cmd(0x05, 0, 0);
        /* 8.0.110 하드 이미지 안 파일(표식 경로 01h + read_at) */
        n = 0; sp[n++] = 3; for (const char *q = "IMG.MV5"; *q; q++) sp[n++] = (uint8_t)*q; sp[n++] = 0; cmd(0x2A, sp, n);
        { uint8_t ec[1]; s = cmd(0x11, op, 12); res(ec, 1); CHECK(!(s & ST_ERR), "이미지 표식 파일 OPEN 오류 코드 %d", ec[0]); }
        cmd(0x08, 0, 0); res(rr, 9); CHECK((rr[0] | rr[1] << 8) == 9, "이미지 파일 INFO 길이 %d", rr[0] | rr[1] << 8);
        cmd(0x03, 0, 0); mmvid_read(0); CHECK(mmvid_playing(), "이미지 파일 PLAY 중"); cmd(0x05, 0, 0);
        /* 없는 파일·다른 드라이브·SETPATH 없이 */
        n = 0; sp[n++] = 4; for (const char *q = "MV\\T.MV5"; *q; q++) sp[n++] = (uint8_t)*q; sp[n++] = 0; cmd(0x2A, sp, n);
        s = cmd(0x11, op, 12); res(rr, 1); CHECK((s & ST_ERR) && rr[0] == 9, "가상 하드·이미지가 아닌 드라이브(E:=4) 는 오류 9 (%d)", rr[0]);
        n = 0; sp[n++] = 2; for (const char *q = "NOPE.MV5"; *q; q++) sp[n++] = (uint8_t)*q; sp[n++] = 0; cmd(0x2A, sp, n);
        s = cmd(0x11, op, 12); res(rr, 1); CHECK((s & ST_ERR) && rr[0] == 8, "없는 파일은 오류 8 (%d)", rr[0]);
        mmvid_reset(); s = cmd(0x11, op, 12); res(rr, 1); CHECK((s & ST_ERR) && rr[0] == 8, "SETPATH 없이 FFFDh 는 오류 8");
        mmvid_set_disk(0); mmvid_reset(); n = 0; sp[n++] = 2; sp[n++] = 'X'; sp[n++] = 0; cmd(0x2A, sp, n); s = cmd(0x11, op, 12); res(rr, 1); CHECK((s & ST_ERR) && rr[0] == 8, "디스크 장치 없으면 FFFDh 오류 8");
        mmvid_set_disk(&disk_ops);
    }
    /* 9 호스트 없으면 */
    mmusb_set_host(0); mmvid_reset(); cmd(0x01, 0, 0); res(r, 5); CHECK(!(r[4] & 1), "호스트 없으면 능력 비트 0");
    s = cmd(0x20, 0, 0); CHECK(s & ST_ERR, "호스트 없으면 오류");
    if (fails) { printf("mmusbtest: %d FAIL\n", fails); return 1; }
    printf("mmusbtest: all passed\n"); return 0;
}
