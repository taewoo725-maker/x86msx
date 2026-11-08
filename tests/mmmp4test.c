/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
#define _POSIX_C_SOURCE 200809L
/* X86 Multimedia MP4 1단계 (8.0.90) 시험: ffmpeg 로 만든 mp4 들을 열어 INFO·오류 코드·목록·IDENT 비트·USB 걸러내기 확인 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>
#include "../src/msx.h"
#include "../src/mmvid.h"
#include "../src/mvsnd.h"
#include "../src/mp4info.h"
#include "../src/mp4play.h"
#include "../host/msx_host.h"

static MSX m; static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
#define ROOT "/tmp/mmmp4_test/Video"
static int h_list(const char *dir, MmEnt *out, int max)
{
    DIR *d = opendir(dir); if (!d) return -1; struct dirent *e; int k = 0;
    while ((e = readdir(d)) && k < max) { char p[512]; snprintf(p, sizeof p, "%s/%s", dir, e->d_name); struct stat st; if (stat(p, &st)) continue;
        snprintf(out[k].name, sizeof out[k].name, "%s", e->d_name); out[k].is_dir = S_ISDIR(st.st_mode); out[k].size = (uint32_t)st.st_size; k++; }
    closedir(d); return k;
}
static int h_read(const char *path, uint32_t off, uint8_t *buf, uint32_t len)
{ FILE *f = fopen(path, "rb"); if (!f) return -1; fseek(f, (long)off, SEEK_SET); size_t n = fread(buf, 1, len, f); fclose(f); if (n < len) memset(buf + n, 0, len - n); return 0; }
static const MmHost host = { h_list, h_read, ROOT, '/' };
static void w(int p, int v) { msx_io_write(&m, p, (uint8_t)v); }
static int r(int p) { return msx_io_read(&m, p); }
static void cmd(int c, int n, const uint8_t *a) { w(0x24, c); for (int i = 0; i < n; i++) w(0x26, a[i]); }
static void frames(int n) { for (int i = 0; i < n; i++) msx_run_frame(&m); }   /* 프레임마다 msx.c 가 믹서에서 소리를 가져감 (mvsnd_render) */
static int rd_file(void *ctx, uint32_t off, uint8_t *b, uint32_t n) { return h_read((const char *)ctx, off, b, n); }

static int open_item(int it, int scr)       /* 0 폴더의 it 번째 항목 OPEN → 상태 bit3 */
{ uint8_t a[12] = { 0, 0, (uint8_t)it, (uint8_t)(it >> 8), (uint8_t)scr, 192, 24, 40, 1, 100, 0, 0 }; cmd(0x11, 12, a); int e = r(0x24) & 8; if (e && getenv("DBG")) printf("  open err code %d\n", r(0x25)); return e; }
static int find(const char *name)
{
    for (int i = 0; i < 40; i++) { uint8_t a[4] = { 0, 0, (uint8_t)i, 0 }; cmd(0x10, 4, a); uint8_t b[15]; for (int k = 0; k < 15; k++) b[k] = (uint8_t)r(0x25);
        if (b[0] == 0xFF) return -1; if (b[0] == 0 && !strncmp((char *)b + 1, name, strlen(name)) && (strlen(name) >= 12 || !b[1 + strlen(name)])) return i; }
    return -1;
}
int main(int argc, char **argv)
{
    char err[256];
    int rc = system("rm -rf /tmp/mmmp4_test; mkdir -p " ROOT " && cd " ROOT " && "
        "F='ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=15:duration=4,format=yuv420p -f lavfi -i sine=frequency=440:duration=4';"
        "$F -c:v libx264 -profile:v baseline -c:a aac base.mp4 && "
        "$F -c:v libx264 -profile:v high -c:a aac high.m4v && "
        "$F -c:v libx264 -profile:v main -c:a aac -f mov main.mov && "
        "$F -c:v libx264 -profile:v high444 -vf format=yuv444p -c:a aac p444.mp4 && "
        "$F -c:v mpeg4 -c:a aac mp4v.mp4 && "
        "$F -c:v libx265 -tag:v hvc1 -c:a aac hevc.mp4 && "
        "$F -c:v libx264 -profile:v baseline -movflags +frag_keyframe+empty_moov frag.mp4 && "
        "ffmpeg -loglevel error -y -f lavfi -i testsrc=size=1280x720:rate=30:duration=2,format=yuv420p -c:v libx264 -profile:v high -an -movflags +faststart big.mp4 && "
        "ffmpeg -loglevel error -y -f lavfi -i sine=frequency=440:duration=2 -c:a aac aonly.mp4 && "
        "for c in pcm_s16le pcm_s16be pcm_u8 pcm_s24be pcm_s32be alac; do ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=15:duration=4,format=yuv420p -f lavfi -i sine=frequency=440:sample_rate=44100:duration=4 -ac 2 -c:v libx264 -profile:v baseline -g 15 -c:a $c p_${c#pcm_}.mov || exit 1; done && "
        "ffmpeg -loglevel error -y -f lavfi -i color=c=red:size=320x240:rate=15:duration=2,format=yuv420p -c:v libx264 -profile:v baseline -an red.mp4 && "
        "ffmpeg -loglevel error -y -f lavfi -i testsrc2=size=640x360:rate=15:duration=6,format=yuv420p -f lavfi -i sine=frequency=330:duration=6 -c:v libx264 -profile:v baseline -g 15 -c:a aac mot.mp4 && "
        "ffmpeg -loglevel error -y -f lavfi -i testsrc2=size=1280x720:rate=15:duration=3,format=yuv420p -c:v libx264 -profile:v baseline -g 15 -an hd.mp4 && "
        "ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=15:duration=4,format=yuv420p -f lavfi -i sine=frequency=1000:duration=3 -c:v libx264 -profile:v baseline -c:a aac -ar 22050 -ac 2 st22.mp4 && "
        "ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=15:duration=4,format=yuv420p -f lavfi -i sine=frequency=440:duration=2 -c:v libx264 -profile:v baseline -c:a aac vlong.mp4 && "
        "ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=15:duration=2,format=yuv420p -f lavfi -i sine=frequency=440:duration=4 -c:v libx264 -profile:v baseline -c:a aac alonger.mp4 && "
        "ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=15:duration=2,format=yuv420p -f lavfi -i sine=frequency=440:duration=2 -c:v libx264 -profile:v baseline -c:a libmp3lame mp3a.mp4 && "
        "$F -c:v mjpeg -pix_fmt yuvj420p -c:a pcm_s16le m.avi && "
        "$F -c:v libx264 -profile:v baseline -c:a pcm_s16le h264.avi && "
        "( [ -f /tmp/mmmp4_long.mp4 ] || ( ffmpeg -loglevel error -y -f lavfi -i testsrc=size=64x64:rate=50:duration=6200,format=yuv420p -c:v libx264 -preset ultrafast -profile:v baseline -g 50 /tmp/mmmp4_lv.mp4 && ffmpeg -loglevel error -y -f lavfi -i sine=frequency=440:duration=6200 -c:a aac -b:a 24k -ar 48000 -ac 1 /tmp/mmmp4_la.m4a && ffmpeg -loglevel error -y -i /tmp/mmmp4_lv.mp4 -i /tmp/mmmp4_la.m4a -map 0:v -map 1:a -c copy /tmp/mmmp4_lm.mp4 && mv /tmp/mmmp4_lm.mp4 /tmp/mmmp4_long.mp4 ) ) && ln -s /tmp/mmmp4_long.mp4 long.mp4 && "
        "head -c 3000 base.mp4 > trunc.mp4 && echo hello > fake.mp4 && : > empty.mp4");
    if (rc) { printf("ffmpeg 준비 실패\n"); return 1; }
    /* --- mp4_probe 직접 --- */
    struct { const char *n; int e; int w, h, fps100; int audio; } T[] = {
        { "base.mp4", MP4E_NONE, 320, 240, 1500, 1 }, { "high.m4v", MP4E_NONE, 320, 240, 1500, 1 }, { "main.mov", MP4E_NONE, 320, 240, 1500, 1 },
        { "p444.mp4", MP4E_NONE, 320, 240, 1500, 1 }, { "mp4v.mp4", MP4E_NONE, 320, 240, 1500, 1 }, { "hevc.mp4", MP4E_VCODEC, 320, 240, 1500, 1 },
        { "big.mp4", MP4E_NONE, 1280, 720, 3000, 0 }, { "aonly.mp4", MP4E_NOVIDEO, 0, 0, 0, 1 }, { "fake.mp4", MP4E_READ, 0, 0, 0, 0 }, { "empty.mp4", MP4E_READ, 0, 0, 0, 0 },
    };
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) {
        char p[256]; snprintf(p, sizeof p, ROOT "/%s", T[i].n); struct stat st; stat(p, &st); Mp4Info mi;
        int e = mp4_probe(rd_file, p, (uint32_t)st.st_size, &mi);
        CHECK(e == T[i].e, "%s probe err %d want %d", T[i].n, e, T[i].e);
        if (T[i].e == MP4E_NONE || T[i].e == MP4E_VCODEC) CHECK(mi.width == T[i].w && mi.height == T[i].h && mi.has_audio == T[i].audio && mi.fps100 >= T[i].fps100 - 20 && mi.fps100 <= T[i].fps100 + 20, "%s info %dx%d fps%u audio%d", T[i].n, mi.width, mi.height, mi.fps100, mi.has_audio);
        if (T[i].e == MP4E_NONE) CHECK(mi.dur_ms >= 1900 && mi.dur_ms <= 4200, "%s dur %u", T[i].n, mi.dur_ms);
        if (!strcmp(T[i].n, "base.mp4")) CHECK(mi.vcodec == 0x61766331 && mi.avc_profile == 66 && mi.aac_obj == 0x40 && mi.channels >= 1, "base avc profile %d aac %02X ch %d", mi.avc_profile, mi.aac_obj, mi.channels);
        if (!strcmp(T[i].n, "main.mov")) CHECK(mi.aac_obj == 0x40 && mi.ta_asc_len > 0, "main.mov (QuickTime): the esds inside the wave box is found, aac %02X asc_len %u (8.0.148)", mi.aac_obj, mi.ta_asc_len);
        if (!strcmp(T[i].n, "high.m4v")) CHECK(mi.avc_profile == 100, "high profile %d", mi.avc_profile);
        if (!strcmp(T[i].n, "p444.mp4")) CHECK(mi.avc_profile == 244, "444 profile %d", mi.avc_profile);
    }
    { char p[256]; snprintf(p, sizeof p, ROOT "/trunc.mp4"); Mp4Info mi; int e = mp4_probe(rd_file, p, 3000, &mi); CHECK(e != MP4E_NONE, "truncated must fail (%d)", e); }
    { char p[256]; snprintf(p, sizeof p, ROOT "/frag.mp4"); struct stat st; stat(p, &st); Mp4Info mi; int e = mp4_probe(rd_file, p, (uint32_t)st.st_size, &mi); CHECK(e == MP4E_NOMOOV || e == MP4E_READ || e == MP4E_NONE, "frag %d", e); printf("frag.mp4 → 오류코드 %d (조각 MP4)\n", e); }
    /* --- 카트리지 경유 --- */

    /* --- 8.0.93 소리: AAC 해독 직접 (주파수·크기), 스테레오 22 kHz 내림 섞기 --- */
    {
        const char *names[] = { "base.mp4", "st22.mp4", "main.mov" }; int want_hz[] = { 440, 1000, 440 };   /* 8.0.148: MOV(QuickTime) 의 소리도 */
        for (int k = 0; k < 3; k++) {
            char p[256]; snprintf(p, sizeof p, ROOT "/%s", names[k]); struct stat st; stat(p, &st);
            int e = mp4p_open(rd_file, p, (uint32_t)st.st_size, 5, 192, 1, 0, 0); CHECK(e == MP4E_NONE, "%s mp4p_open %d", names[k], e);
            CHECK(mp4p_audio_open() == 1, "%s audio open", names[k]);
            uint32_t ar0 = 0, ar_first = 0; static int16_t mono[48000 * 5]; int nm = 0, nfr = 0, rate = 0, chn = 0; const int16_t *pcm; int fr, ch, hz;
            while (mp4p_audio_next(&pcm, &fr, &ch, &hz) && nm + fr < (int)(sizeof mono / sizeof mono[0])) { for (int i = 0; i < fr; i++) { int v = 0; for (int c = 0; c < ch; c++) v += pcm[i * ch + c]; mono[nm++] = (int16_t)(v / ch); } if (nfr == 0) ar_first = mp4p_arena_used(); nfr++; rate = hz; chn = ch; }
            ar0 = ar_first; CHECK(mp4p_arena_used() == ar0, "%s arena stable after the first frame (%u -> %u)", names[k], ar0, mp4p_arena_used());
            double sum = 0; int cross = 0, a = 4096, b = nm > 4096 + 8192 ? 4096 + 8192 : nm; for (int i = a; i < b; i++) { sum += (double)mono[i] * mono[i]; if (i > a && ((mono[i - 1] < 0) != (mono[i] < 0))) cross++; }
            double rms = sqrt(sum / (b - a)), f = cross / 2.0 / ((b - a) / (double)rate);
            printf("%s: AAC %d프레임 %d Hz %dch rms %.0f 주파수 %.0f Hz\n", names[k], nfr, rate, chn, rms, f);
            CHECK(rms > 1000 && rms < 20000 && f > want_hz[k] * 0.97 && f < want_hz[k] * 1.03, "%s tone rms %.0f freq %.0f (want %d)", names[k], rms, f, want_hz[k]);
            CHECK(nfr * 1024 >= (k == 1 ? 3 : 4) * rate - 4096 && nfr * 1024 <= (k == 1 ? 3 : 4) * rate + 4096, "%s frame count %d", names[k], nfr);
            uint32_t sk = mp4p_audio_seek(2000); CHECK(sk >= 1900 && sk <= 2100, "audio seek 2000 -> %u", sk);
            CHECK(mp4p_audio_next(&pcm, &fr, &ch, &hz) == 1, "frame after seek");
            mp4p_close();
        }
        char p[256]; snprintf(p, sizeof p, ROOT "/mp3a.mp4"); struct stat st; stat(p, &st);
        if (mp4p_open(rd_file, p, (uint32_t)st.st_size, 5, 192, 1, 0, 0) == MP4E_NONE) { CHECK(mp4p_audio_open() == 0, "mp3-in-mp4: no audio (video only)"); mp4p_close(); }
        snprintf(p, sizeof p, ROOT "/red.mp4"); stat(p, &st);
        if (mp4p_open(rd_file, p, (uint32_t)st.st_size, 5, 192, 1, 0, 0) == MP4E_NONE) { CHECK(mp4p_audio_open() == 0, "no audio track"); mp4p_close(); }
    }
    if (msx_host_init(&m, argc > 1 ? argv[1] : "tests/machines/cbios2p", err, sizeof err)) { printf("init: %s\n", err); return 1; }
    mmvid_set_host(&host);
    static uint8_t blank[16384]; memset(blank, 0xFF, sizeof blank);
    CHECK(msx_insert_cart(&m, 1, blank, sizeof blank, MAPPER_MMEDIA) == 0, "insert");
    msx_reset(&m); frames(120);
    msx_mem_write(&m, 0xC000, 0xF3); msx_mem_write(&m, 0xC001, 0x18); msx_mem_write(&m, 0xC002, 0xFE); m.cpu.pc = 0xC000; frames(2);
    cmd(0x01, 0, 0); CHECK(r(0x25) == 'X' && r(0x25) == 'V' && r(0x25) == 'D' && r(0x25) == 1, "IDENT");
    int id5 = r(0x25); CHECK((id5 & 8) == 8 && (id5 & 4), "mp4 cap on by default (5th %02X)", id5);
    cmd(0x01, 0, 0); r(0x25); r(0x25); r(0x25); r(0x25); id5 = r(0x25); CHECK((id5 & 8) == 8, "cap on: bit3 (5th %02X)", id5);
    int ib = find("base.mp4"); CHECK(ib >= 0, "cap on: base.mp4 listed"); CHECK(find("high.m4v") >= 0 && find("main.mov") >= 0, "m4v/mov listed"); CHECK(find("trunc.mp4") >= 0, "bad mp4 also listed");
    /* --- 8.0.91 재생 (MSX1 은 비트맵 화면이 없어 코드 6) --- */
    int msx1 = !m.vdp.v9938;
    if (msx1) { CHECK(open_item(ib, 5) == 8 && r(0x25) == MP4E_SCREEN, "MSX1: SCREEN 5 -> code 6"); printf("MSX1 기종: 재생 시험 생략 (SCREEN 코드 6 확인)\n"); } else {
    CHECK(open_item(ib, 5) == 0, "base open SCREEN 5 ok"); 
    cmd(0x08, 0, 0); { int v[5]; for (int i = 0; i < 4; i++) { int lo = r(0x25); int hi = r(0x25); v[i] = lo | hi << 8; } v[4] = r(0x25);
        CHECK(v[0] >= 55 && v[0] <= 65 && v[1] == 320 && v[2] == 240 && v[3] >= 1480 && v[3] <= 1520 && v[4] == 1, "INFO len %d %dx%d fps %d au %d", v[0], v[1], v[2], v[3], v[4]); }
    CHECK(m.vdp.reg[0] == 0x06 && (m.vdp.reg[1] & 0x40), "SCREEN 5 regs R0=%02X R1=%02X", m.vdp.reg[0], m.vdp.reg[1]);
    { int nz = 0; for (int i = 0; i < 24576; i++) nz += m.vdp.vram[i] != 0; CHECK(nz > 1000, "first frame in VRAM (nonzero %d)", nz); }
    cmd(0x03, 0, 0); frames(60); w(0x24, 0x09); { int p = r(0x26) | (r(0x27) << 8); CHECK(p >= 13 && p <= 17, "POS after 1s = %d (1/15 s)", p); }
    cmd(0x04, 0, 0); w(0x24, 0x09); int p1 = r(0x26) | (r(0x27) << 8); frames(30); w(0x24, 0x09); CHECK((r(0x26) | (r(0x27) << 8)) == p1, "PAUSE holds POS");
    { uint8_t a[2] = { 30, 0 }; cmd(0x06, 2, a); w(0x24, 0x09); int p = r(0x26) | (r(0x27) << 8); CHECK(p <= 30 && p >= 0, "SEEK 30 -> keyframe pos %d", p); CHECK(!(r(0x24) & 8), "SEEK no err"); }
    cmd(0x03, 0, 0); frames(60 * 5); CHECK(r(0x24) & 4, "END bit after the video finishes"); 
    cmd(0x05, 0, 0);
    /* 단색 빨강: 첫 점 색이 빨강 계열 */
    { int ir = find("red.mp4"); CHECK(ir >= 0, "red listed"); CHECK(open_item(ir, 5) == 0, "red open"); int c0 = m.vdp.vram[0] >> 4; CHECK(m.vdp.palette[c0][0] > 150 && m.vdp.palette[c0][1] < 90 && m.vdp.palette[c0][2] < 90, "red pixel palette %d,%d,%d", m.vdp.palette[c0][0], m.vdp.palette[c0][1], m.vdp.palette[c0][2]); cmd(0x05, 0, 0); }
    /* 다른 화면 */
    CHECK(open_item(ib, 6) == 0 && m.vdp.reg[0] == 0x08, "SCREEN 6 R0=%02X", m.vdp.reg[0]); cmd(0x05, 0, 0);
    CHECK(open_item(ib, 7) == 0 && m.vdp.reg[0] == 0x0A, "SCREEN 7 R0=%02X", m.vdp.reg[0]); cmd(0x05, 0, 0);
    CHECK(open_item(ib, 8) == 0 && m.vdp.reg[0] == 0x0E, "SCREEN 8 R0=%02X", m.vdp.reg[0]); cmd(0x05, 0, 0);
    /* 8.0.149 파란 표시줄: SCREEN 5·8 은 212줄로 열려 192..211 줄에 표시줄. 글자 줄이 꺼지면 한 색, 켜지면 알약·시간·볼륨 등으로 여러 색. Mp4Bar=0 이면 192줄 그대로 */
    for (int bs = 0; bs < 1; bs++) {
        int scrs[2] = { 5, 8 };
        for (int q = 0; q < 2; q++) {
            int sc = scrs[q], rb = sc == 5 ? 128 : 256;
            CHECK(open_item(ib, sc) == 0, "bar S%d open", sc);
            CHECK(((m.vdp.reg[9] & 0x80) != 0) == (bs == 0), "bar S%d bit R9: 212줄 %d (표시줄 %d)", sc, !!(m.vdp.reg[9] & 0x80), bs == 0);
            if (bs == 0) {
                int distinct = 0; uint8_t seen[256] = { 0 };
                for (int y = 192; y < 212; y++) for (int x = 0; x < rb; x++) { uint8_t v = m.vdp.vram[y * rb + x]; if (!seen[v]) { seen[v] = 1; distinct++; } }
                CHECK(distinct == 1, "bar S%d 글자 줄 꺼짐: 표시줄은 한 색 (%d)", sc, distinct);
                { uint8_t on1[1] = { 1 }; cmd(0x0E, 1, on1); } frames(3);
                distinct = 0; memset(seen, 0, sizeof seen);
                for (int y = 192; y < 212; y++) for (int x = 0; x < rb; x++) { uint8_t v = m.vdp.vram[y * rb + x]; if (!seen[v]) { seen[v] = 1; distinct++; } }
                CHECK(distinct >= 3, "bar S%d 글자 줄 켜짐: 표시줄 색 %d가지", sc, distinct);
                { uint8_t lv[1] = { 25 }; cmd(0x07, 1, lv); } frames(3);                         /* 볼륨 25% → 4단계: 볼륨 칸이 바뀜 */
                if (sc == 8) {                                                                   /* 8.0.152 이름표(216..247, 4..17: 바탕 27h)와 일시정지 큰 표시(112..143, 76..115: 테두리 FFh) */
                    int bg = 0, rim = 0; for (int y = 4; y < 18; y++) for (int x = 216; x < 248; x++) bg += m.vdp.vram[y * 256 + x] == 0x27;
                    CHECK(bg > 250, "badge S8: 파란 바탕 %d점", bg);
                    cmd(0x03, 0, 0); frames(3); cmd(0x04, 0, 0); frames(3);
                    for (int x = 115; x < 141; x++) rim += m.vdp.vram[76 * 256 + x] == 0xFF;
                    CHECK(rim >= 20, "pause sign S8: 위 테두리 %d점", rim);
                    cmd(0x03, 0, 0); frames(3);
                    rim = 0; for (int x = 115; x < 141; x++) rim += m.vdp.vram[76 * 256 + x] == 0xFF;
                    CHECK(rim < 20, "pause sign S8: 재생하면 사라짐 (%d)", rim);
                }
                { uint8_t on0[1] = { 0 }; cmd(0x0E, 1, on0); } frames(3);
                distinct = 0; memset(seen, 0, sizeof seen);
                for (int y = 192; y < 212; y++) for (int x = 0; x < rb; x++) { uint8_t v = m.vdp.vram[y * rb + x]; if (!seen[v]) { seen[v] = 1; distinct++; } }
                CHECK(distinct == 1, "bar S%d 글자 줄 다시 꺼짐 (%d)", sc, distinct);
            }
            cmd(0x05, 0, 0);
        }
    }
    CHECK(open_item(ib, 10) == 8 && r(0x25) == MP4E_SCREEN, "SCREEN 10 -> code 6 for now");
    CHECK(open_item(ib, 3) == 8 && r(0x25) == MP4E_SCREEN, "SCREEN 3 -> code 6");
    /* 8.0.92 SCREEN 12 (YJK, V9958 이상만) / VRAM 64KB 면 SCREEN 5 로 */
    CHECK(open_item(ib, 11) == 8 && r(0x25) == MP4E_SCREEN, "SCREEN 11 -> code 6");
    if (!m.vdp.v9958) { CHECK(open_item(ib, 12) == 8 && r(0x25) == MP4E_SCREEN, "V9938: SCREEN 12 -> code 6"); printf("V9938 기종: SCREEN 12 은 코드 6 확인\n"); }
    else {
        int ir = find("red.mp4"); CHECK(open_item(ir, 12) == 0, "S12 red open");
        CHECK(m.vdp.reg[0] == 0x0E && (m.vdp.reg[25] & 0x08) && !(m.vdp.reg[25] & 0x10), "S12 regs R0=%02X R25=%02X", m.vdp.reg[0], m.vdp.reg[25]);
        { uint8_t raw[256], rgb[256][3], pi[256];
          for (int i = 0; i < 256; i++) raw[i] = m.vdp.vram[i];   /* 배열은 늘 논리 주소 순서 */
          vdp_yjk_line(raw, 0, rgb, pi);
          if (getenv("DBG")) { for (int i = 8; i < 16; i++) printf("raw[%d]=%02X ", i, raw[i]); printf("\n"); }
          CHECK(rgb[10][0] > 150 && rgb[10][1] < 90 && rgb[10][2] < 90, "S12 red pixel %d,%d,%d", rgb[10][0], rgb[10][1], rgb[10][2]); }
        cmd(0x05, 0, 0);
        CHECK(open_item(ib, 12) == 0, "S12 base open"); { int nz = 0; for (int i = 0; i < 40000; i++) nz += m.vdp.vram[i] != 0; CHECK(nz > 3000, "S12 first frame nonzero %d", nz); }
        cmd(0x03, 0, 0); frames(60); w(0x24, 0x09); { int p = r(0x26) | (r(0x27) << 8); CHECK(p >= 13 && p <= 17, "S12 POS after 1s = %d", p); }
        { uint8_t a[2] = { 30, 0 }; cmd(0x06, 2, a); CHECK(!(r(0x24) & 8), "S12 SEEK no err"); }
        cmd(0x03, 0, 0); frames(60 * 5); CHECK(r(0x24) & 4, "S12 END"); cmd(0x05, 0, 0);
    }
    { uint32_t sv = m.vdp.vram_mask; m.vdp.vram_mask = 0xFFFF;
      CHECK(open_item(ib, 8) == 0 && m.vdp.reg[0] == 0x06, "64KB: P4=8 -> SCREEN 5 (R0=%02X)", m.vdp.reg[0]); cmd(0x03, 0, 0); frames(30); cmd(0x05, 0, 0);
      if (m.vdp.v9958) { CHECK(open_item(ib, 12) == 0 && m.vdp.reg[0] == 0x06, "64KB: P4=12 -> SCREEN 5"); cmd(0x05, 0, 0); }
      CHECK(open_item(ib, 7) == 8 && r(0x25) == MP4E_SCREEN, "64KB: SCREEN 7 -> code 6");
      m.vdp.vram_mask = sv; }
    CHECK(open_item(ib, 3) == 8 && r(0x25) == MP4E_SCREEN, "SCREEN 3 -> code 6");
    /* 글자 줄 */
    { static uint8_t ov[4096]; memset(ov, 0, sizeof ov); for (int i = 0; i < 2048; i++) ov[i] = 0xFF; for (int i = 2048; i < 4096; i++) ov[i] = 0x10;   /* 앞색 1, 뒷색 0 */
      CHECK(open_item(ib, 5) == 0, "reopen"); cmd(0x0D, 0, 0); for (int i = 0; i < 4096; i++) w(0x25, ov[i]); { uint8_t a[1] = { 1 }; cmd(0x0E, 1, a); } frames(2);
      int c = m.vdp.vram[0] & 15; int c2 = m.vdp.vram[0] >> 4; CHECK(c == c2, "overlay top rows are solid (%x %x)", c2, c);
      CHECK(m.vdp.vram[10 * 128 + 5] == m.vdp.vram[0], "overlay row 10 same");
      { uint8_t a[1] = { 0 }; cmd(0x0E, 1, a); frames(2); } cmd(0x05, 0, 0); }
    }
    /* 8.0.140 글자 줄(OVL)이 SCREEN 5·6·7·8·12 어디서나 위·아래에 얹히는지 (X86MV 요청서: SCREEN 2 에서만 보인다는 보고) */
    { static const int scrs[] = { 5, 6, 7, 8, 12 }; static uint8_t ov[4096]; memset(ov, 0, sizeof ov); for (int i = 0; i < 2048; i++) ov[i] = 0xFF; for (int i = 2048; i < 4096; i++) ov[i] = 0x10;
      for (unsigned k = 0; k < sizeof scrs / sizeof scrs[0]; k++) {
        int sc = scrs[k]; if (sc == 12 && !m.vdp.v9958) continue; if (sc >= 7 && sc != 12 && sc != 8 && m.vdp.vram_mask < 0x1FFFF) continue;
        int rb = (sc == 5 || sc == 6) ? 128 : 256; int H = 192;
        CHECK(open_item(ib, sc) == 0, "ovl open scr %d", sc);
        { uint8_t a1[1] = { 0 }; cmd(0x0E, 1, a1); } frames(2);
        int solid_off = 1; for (int x = 1; x < rb; x++) if (m.vdp.vram[x] != m.vdp.vram[0]) solid_off = 0;
        cmd(0x0D, 0, 0); for (int i = 0; i < 4096; i++) w(0x25, ov[i]); { uint8_t a1[1] = { 1 }; cmd(0x0E, 1, a1); } frames(2);
        int top = 1, bot = 1; int msk = sc == 12 ? 0xF8 : 0xFF;   /* 8.0.143 YJK 는 아래 3비트(J·K 조각)가 초록 때문에 달라짐 → Y(위 5비트)만 비교 */
        for (int x = 1; x < rb; x++) { if ((m.vdp.vram[x] & msk) != (m.vdp.vram[0] & msk)) top = 0; if ((m.vdp.vram[(H - 1) * rb + x] & msk) != (m.vdp.vram[(H - 1) * rb] & msk)) bot = 0; }
        CHECK(solid_off || !(top && bot), "scr %d: 8.0.161 파란 표시줄이 있으면 옛 초록 글자 줄은 안 얹힘 (top %d bot %d)", sc, top, bot);
        cmd(0x05, 0, 0); } }
    /* 8.0.93 소리 시계: AAC 가 있으면 영상 시계 = 믹서가 내보낸 48 kHz 샘플 수 */
    if (!msx1) {
        CHECK(open_item(ib, 5) == 0, "au open"); CHECK(mvsnd_ext_active(), "ext active (AAC found)");
        uint32_t e0 = mvsnd_ext_elapsed(); CHECK(e0 == 0, "elapsed 0 before PLAY (%u)", e0);
        cmd(0x03, 0, 0); frames(60); w(0x24, 0x09); { int p = r(0x26) | (r(0x27) << 8); uint32_t el = mvsnd_ext_elapsed();
            CHECK(el > 44000 && el < 52000, "elapsed after 1 s = %u", el); CHECK(p >= 13 && p <= 17, "POS follows sound %d", p); }
        cmd(0x04, 0, 0); frames(2); uint32_t ep = mvsnd_ext_elapsed(); frames(30); CHECK(mvsnd_ext_elapsed() == ep, "PAUSE holds sound clock"); CHECK(!mvsnd_playing(), "mixer not playing while paused");
        cmd(0x03, 0, 0); frames(30); CHECK(mvsnd_ext_elapsed() > ep + 20000, "resume continues (%u -> %u)", ep, mvsnd_ext_elapsed());
        cmd(0x05, 0, 0);
        { int imot = find("mot.mp4"); CHECK(imot >= 0 && open_item(imot, 5) == 0, "mot open"); cmd(0x03, 0, 0); frames(20);
          uint8_t a[2] = { 30, 0 }; cmd(0x06, 2, a); w(0x24, 0x09); int p = r(0x26) | (r(0x27) << 8); CHECK(p >= 28 && p <= 31, "SEEK 30 -> POS %d", p); CHECK(mvsnd_ext_elapsed() < 4000, "SEEK resets sound clock (%u)", mvsnd_ext_elapsed());
          frames(60); w(0x24, 0x09); p = r(0x26) | (r(0x27) << 8); CHECK(p >= 43 && p <= 47, "1 s after SEEK -> POS %d", p); cmd(0x05, 0, 0); }
        CHECK(open_item(ib, 5) == 0, "au reopen"); cmd(0x03, 0, 0); frames(10);
        { uint8_t a[1] = { 0 }; cmd(0x07, 1, a); frames(10); CHECK(1, "volume 0 ok"); a[0] = 100; cmd(0x07, 1, a); }
        frames(60 * 4); CHECK(r(0x24) & 4, "au END bit"); CHECK(!mvsnd_playing(), "sound stops at END");
        cmd(0x03, 0, 0); frames(30); { w(0x24, 0x09); int p = r(0x26) | (r(0x27) << 8); CHECK(p >= 5 && p <= 10, "PLAY after END restarts (POS %d)", p); CHECK(mvsnd_playing(), "sound restarts"); }
        cmd(0x05, 0, 0); CHECK(!mvsnd_ext_active(), "STOP closes sound");
        /* 소리가 영상보다 짧음: 프레임 시계로 이어서 끝까지 */
        int iv = find("vlong.mp4"); CHECK(iv >= 0 && open_item(iv, 5) == 0, "vlong open"); cmd(0x03, 0, 0); frames(60 * 3); CHECK(!(r(0x24) & 4), "vlong not ended at 3 s"); frames(60 * 2); CHECK(r(0x24) & 4, "vlong ends after video (4 s)"); cmd(0x05, 0, 0);
        /* 소리가 영상보다 김: 영상 끝에서 끝 */
        int ia = find("alonger.mp4"); CHECK(ia >= 0 && open_item(ia, 5) == 0, "alonger open"); cmd(0x03, 0, 0); frames(60 * 3); CHECK(r(0x24) & 4, "alonger ends with video (2 s)"); CHECK(!mvsnd_playing(), "alonger sound stopped"); cmd(0x05, 0, 0);
        /* 소리 없는 영상·MP3 소리 영상: 프레임 시계 */
        int ir2 = find("red.mp4"); CHECK(open_item(ir2, 5) == 0 && !mvsnd_ext_active(), "no audio -> no ext"); cmd(0x03, 0, 0); frames(60); { w(0x24, 0x09); int p = r(0x26) | (r(0x27) << 8); CHECK(p >= 13 && p <= 17, "no-audio POS %d", p); } cmd(0x05, 0, 0);
        int im3 = find("mp3a.mp4"); if (im3 >= 0) { CHECK(open_item(im3, 5) == 0 && !mvsnd_ext_active(), "mp3-in-mp4 plays video only"); cmd(0x05, 0, 0); }
        /* 다른 영상으로 갈아 열기: 소리 상태가 남지 않음 */
        CHECK(open_item(ib, 5) == 0 && mvsnd_ext_active(), "reopen"); CHECK(open_item(ir2, 5) == 0 && !mvsnd_ext_active(), "reopen without audio closes ext"); cmd(0x05, 0, 0);
    }
    /* 8.0.122 긴 영상: 영상 샘플 31만 개(6200초 50fps) + 소리 샘플 29만 개(48kHz AAC) → 열 때 표를 크기에 맞춰 잡음 */
    if (!msx1) {
        int il = find("long.mp4"); CHECK(il >= 0, "long.mp4 있음");
        { int oo = open_item(il, 5); CHECK(il >= 0 && oo == 0, "long open (code %d)", r(0x25)); }
        CHECK(mvsnd_ext_active(), "long: 소리 표도 잡혀 소리 재생");
        cmd(0x03, 0, 0); frames(60); w(0x24, 0x09); { int p = r(0x26) | (r(0x27) << 8); uint32_t el = mvsnd_ext_elapsed();
            CHECK(el > 44000 && el < 52000, "long elapsed after 1 s = %u", el); CHECK(p >= 13 && p <= 17, "long POS %d", p); }
        { uint8_t a[2] = { 45000 & 255, 45000 >> 8 }; cmd(0x06, 2, a); w(0x24, 0x09); int p = r(0x26) | (r(0x27) << 8); if (getenv("DBG")) printf("  seek: POS %d elapsed %u\n", p, mvsnd_ext_elapsed()); CHECK(p >= 44990 && p <= 45010, "long SEEK 3000 s -> POS %d", p); }
        frames(60); { w(0x24, 0x09); int p = r(0x26) | (r(0x27) << 8); CHECK(p >= 45010 && p <= 45030, "long 1 s after SEEK -> POS %d", p); }
        cmd(0x05, 0, 0);
        CHECK(open_item(find("base.mp4"), 5) == 0, "long 뒤에 보통 영상도 열림 (표가 다시 고정 표로)"); cmd(0x03, 0, 0); frames(30); cmd(0x05, 0, 0);
    }
    /* 8.0.120 AVI (MJPEG + PCM) */
    if (!msx1) {
        int ia2 = find("m.avi");  { int oo = open_item(ia2, 5); CHECK(ia2 >= 0 && oo == 0, "avi open (idx %d code %d)", ia2, r(0x25)); } CHECK(mvsnd_ext_active(), "avi PCM sound active");
        cmd(0x03, 0, 0); frames(60); w(0x24, 0x09); { int p = r(0x26) | (r(0x27) << 8); uint32_t el = mvsnd_ext_elapsed();
            CHECK(el > 44000 && el < 52000, "avi elapsed after 1 s = %u", el); CHECK(p >= 13 && p <= 17, "avi POS %d", p); }
        frames(60 * 4); CHECK(r(0x24) & 4, "avi END"); cmd(0x05, 0, 0);
        int ih2 = find("h264.avi"); CHECK(ih2 >= 0 && open_item(ih2, 5) == 8 && r(0x25) == MP4E_VCODEC, "avi H.264 -> code 3");
    }
    /* 8.0.153 MOV 의 PCM 소리 5종 + ALAC: 열림, 소리 켜짐, 1초 뒤 소리 시계 약 44100점, 이동하면 시계가 따라옴, 끝까지 가면 END */
    if (!msx1) {
        static const char *const pn[6] = { "p_s16le.mov", "p_s16be.mov", "p_u8.mov", "p_s24be.mov", "p_s32be.mov", "p_alac.mov" };
        for (int q = 0; q < 6; q++) {
            int ip = find(pn[q]); int oo = ip >= 0 ? open_item(ip, 5) : -1;
            CHECK(ip >= 0 && oo == 0, "mov %s open (idx %d code %d)", pn[q], ip, oo); if (oo) continue;
            CHECK(mvsnd_ext_active(), "mov %s PCM sound active", pn[q]);
            cmd(0x03, 0, 0); frames(60); { uint32_t el = mvsnd_ext_elapsed(); CHECK(el > 40000 && el < 52000, "mov %s elapsed after 1 s = %u", pn[q], el); }
            { uint8_t a[2] = { 30, 0 }; cmd(0x06, 2, a); w(0x24, 0x09); int p = r(0x26) | (r(0x27) << 8); CHECK(p >= 28 && p <= 31, "mov %s SEEK 30 -> POS %d", pn[q], p); CHECK(mvsnd_ext_elapsed() < 6000, "mov %s SEEK resets sound clock (%u)", pn[q], mvsnd_ext_elapsed()); }
            frames(60 * 4); CHECK(r(0x24) & 4, "mov %s END", pn[q]); cmd(0x05, 0, 0);
        }
    }
    /* 지원 안 하는 것 */
    if (!msx1) {
    /* 8.0.96 2c: High(CABAC·B 프레임·8x8)·Main 도 열려서 그림이 나와야 함 (OpenH264) */
    CHECK(open_item(find("high.m4v"), 5) == 0, "High (CABAC, B-frames) opens"); { uint32_t pw = 0; (void)pw; cmd(0x03, 0, 0); frames(40); w(0x24, 0x09); int p = r(0x26) | (r(0x27) << 8); CHECK(p >= 1, "High plays POS %d", p); } cmd(0x05, 0, 0);
    CHECK(open_item(find("main.mov"), 5) == 0, "Main opens"); cmd(0x03, 0, 0); frames(30); cmd(0x05, 0, 0);
    CHECK(open_item(find("hevc.mp4"), 6) == 8 && r(0x25) == MP4E_VCODEC, "hevc -> code 3");
    CHECK(open_item(find("p444.mp4"), 7) == 8 && r(0x25) == MP4E_PROFILE, "444 -> code 4");
    CHECK(open_item(find("fake.mp4"), 5) == 8 && r(0x25) == MP4E_READ, "fake -> code 1");
    CHECK(open_item(find("trunc.mp4"), 5) == 8, "trunc -> error");
    CHECK(open_item(find("base.mp4"), 2) == 8 && r(0x25) == MP4E_READ, "mp4 as SCREEN 2 -> code 1");
    CHECK(open_item(find("base.mp4"), 5) == 0, "reopen after errors"); cmd(0x05, 0, 0);
    }
    if (!msx1) {
    /* 시간 재기: 640x360 baseline 6초, 1280x720 3초 */
    { int im = find("mot.mp4"); CHECK(im >= 0 && open_item(im, 7) == 0, "mot open S7"); cmd(0x03, 0, 0);
      struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0); frames(60 * 6 + 10); clock_gettime(CLOCK_MONOTONIC, &t1);
      printf("640x360 SCREEN 7 6초 재생 걸린 시간 %.2f 초 (실시간 6초 이내여야)\n", (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9);
      CHECK(r(0x24) & 4, "mot END"); cmd(0x05, 0, 0); }
    { int ih = find("hd.mp4"); CHECK(ih >= 0 && open_item(ih, 8) == 0, "hd open S8"); cmd(0x03, 0, 0);
      struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0); frames(60 * 3 + 10); clock_gettime(CLOCK_MONOTONIC, &t1);
      printf("1280x720 SCREEN 8 3초 재생 걸린 시간 %.2f 초\n", (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9);
      cmd(0x05, 0, 0); }
    }
    printf(fails ? "mmmp4test: %d FAIL\n" : "mmmp4test: all passed\n", fails);
    return fails != 0;
}
