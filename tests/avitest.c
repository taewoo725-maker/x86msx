/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
#define _POSIX_C_SOURCE 200809L
/* 8.0.120 AVI(MJPEG+PCM) 시험: avitest jpg <jpg> <i420.raw> <w> <h>  → 해독 그림이 ffmpeg 와 얼마나 같은지(PSNR)
 *                              avitest avi <avi> <frames> <audio초>  → 열기·재생·소리 길이 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/jpegdec.h"
#include "../src/mp4info.h"
#include "../src/mp4play.h"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
static uint8_t *slurp(const char *p, uint32_t *n) { FILE *f = fopen(p, "rb"); if (!f) return 0; fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = malloc((size_t)l + 16); *n = (uint32_t)fread(b, 1, (size_t)l, f); fclose(f); return b; }
static int rd(void *ctx, uint32_t off, uint8_t *b, uint32_t n) { FILE *f = fopen((const char *)ctx, "rb"); if (!f) return -1; fseek(f, (long)off, SEEK_SET); size_t k = fread(b, 1, n, f); fclose(f); return k == n ? 0 : -1; }
int main(int argc, char **argv)
{
    if (argc >= 6 && !strcmp(argv[1], "jpg")) {
        uint32_t nj, nr; uint8_t *j = slurp(argv[2], &nj), *raw = slurp(argv[3], &nr); int W = atoi(argv[4]), H = atoi(argv[5]);
        static uint8_t out[JPG_OUT_BYTES]; int w = 0, h = 0;
        int e = jpg_decode(j, nj, out, sizeof out, &w, &h);
        CHECK(e == 0 && w == W && h == H, "해독 e=%d %dx%d", e, w, h);
        if (!e) {
            int mw = (w + 1) & ~1, mh = (h + 1) & ~1; double se = 0; long cnt = 0, sec = 0;
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { int d = out[y * mw + x] - raw[y * W + x]; se += d * d; cnt++; }
            double psnr = 10 * log10(255.0 * 255.0 / (se / cnt + 1e-9));
            double sc = 0; for (int i = 0; i < (mw / 2) * (mh / 2) * 2; i++) { int a = out[mw * mh + i]; int ref = raw[W * H + i]; (void)ref; sec++; (void)a; }
            double se2 = 0; long c2 = 0; int cw = (W + 1) / 2, chh = (H + 1) / 2;
            for (int y = 0; y < chh; y++) for (int x = 0; x < cw; x++) { int d = out[mw * mh + y * (mw / 2) + x] - raw[W * H + y * cw + x]; se2 += d * d; c2++; }
            for (int y = 0; y < chh; y++) for (int x = 0; x < cw; x++) { int d = out[mw * mh + (mw / 2) * (mh / 2) + y * (mw / 2) + x] - raw[W * H + cw * chh + y * cw + x]; se2 += d * d; c2++; }
            double pc = 10 * log10(255.0 * 255.0 / (se2 / c2 + 1e-9)); (void)sc;
            printf("PSNR Y %.1f dB, UV %.1f dB\n", psnr, pc);
            CHECK(psnr > 33.0, "Y PSNR %.1f", psnr); CHECK(pc > 32.0, "UV PSNR %.1f", pc);
        }
    } else if (argc >= 5 && !strcmp(argv[1], "avi")) {
        const char *path = argv[2]; int want_frames = atoi(argv[3]); double want_aud = atof(argv[4]);
        uint32_t sz; uint8_t *tmp = slurp(path, &sz); free(tmp);
        Mp4Info mi; int e = mp4_probe(rd, (void *)path, sz, &mi);
        CHECK(e == 0 && mi.avi, "probe e=%d", e);
        printf("probe: %dx%d frames=%u dur=%ums fps100=%u audio=%d ch=%u rate=%u\n", mi.width, mi.height, mi.nframes, mi.dur_ms, mi.fps100, mi.has_audio, mi.channels, mi.rate);
        CHECK((int)mi.nframes == want_frames, "프레임 수 %u", mi.nframes);
        e = mp4p_open(rd, (void *)path, sz, 8, 192, 1, 100, 0);
        CHECK(e == 0, "open e=%d", e);
        if (!e) {
            Mp4Pic pc; int shown = 0; for (uint32_t t = 0; t <= mi.dur_ms + 200; t += 33) { int r = mp4p_frame(t, 0, 0, &pc); if (r > 0) shown++; }
            printf("shown %d, dropped %u\n", shown, mp4p_dropped());
            CHECK(shown > want_frames / 2, "그린 수 %d", shown);
            if (want_aud > 0) {
                CHECK(mp4p_audio_open() == 1, "소리 열기");
                const int16_t *pcm; int n, ch, hz; long tot = 0; int rate = 0;
                while (mp4p_audio_next(&pcm, &n, &ch, &hz)) { tot += n; rate = hz; }
                double sec = rate ? (double)tot / rate : 0; printf("audio %.2f s @%d\n", sec, rate);
                CHECK(fabs(sec - want_aud) < 0.15, "소리 길이 %.2f", sec);
                uint32_t sk = mi.dur_ms > 1500 ? 1000 : mi.dur_ms / 2; uint32_t t2 = mp4p_audio_seek(sk); CHECK(t2 <= sk && t2 + 100 > sk, "소리 이동 %u", t2);
                CHECK(mp4p_audio_next(&pcm, &n, &ch, &hz) == 1, "이동 뒤 소리");
            }
            uint32_t sk2 = mi.dur_ms > 1500 ? 1000 : mi.dur_ms / 2; uint32_t s = mp4p_seek(sk2); CHECK(s <= sk2 && s + 100 >= sk2, "영상 이동 %u", s);
            mp4p_close();
        }
    } else if (argc >= 4 && !strcmp(argv[1], "pcmcmp")) {      /* 8.0.153 avitest pcmcmp <mov> <ffmpeg 가 푼 s16le 원본> : MOV PCM 소리가 원본과 같은지 (오차 1 이하) */
        const char *path = argv[2]; uint32_t sz, nref; uint8_t *tmp = slurp(path, &sz); free(tmp);
        uint8_t *refb = slurp(argv[3], &nref); const int16_t *ref = (const int16_t *)refb; long nrs = nref / 2;
        Mp4Info mi; int e = mp4_probe(rd, (void *)path, sz, &mi); CHECK(e == 0 && mi.has_audio, "probe e=%d", e);
        e = mp4p_open(rd, (void *)path, sz, 8, 192, 1, 100, 0); CHECK(e == 0, "open e=%d", e);
        if (!e) {
            CHECK(mp4p_audio_open() == 1, "소리 열기");
            const int16_t *pcm; int n, ch, hz; long pos = 0, bad = 0, maxd = 0;
            while (mp4p_audio_next(&pcm, &n, &ch, &hz)) {
                for (int i = 0; i < n * ch && pos < nrs; i++, pos++) { long d = pcm[i] - ref[pos]; if (d < 0) d = -d; if (d > maxd) maxd = d; if (d > 1) bad++; }
            }
            printf("compared %ld samples (ref %ld), max diff %ld, bad %ld\n", pos, nrs, maxd, bad);
            CHECK(pos >= nrs - 4096 * 2 && bad == 0, "소리 값이 원본과 다름 (bad %ld, pos %ld/%ld)", bad, pos, nrs);
            uint32_t t2 = mp4p_audio_seek(1000); CHECK(t2 <= 1000 && t2 + 400 > 1000, "소리 이동 %u", t2);
            CHECK(mp4p_audio_next(&pcm, &n, &ch, &hz) == 1, "이동 뒤 소리");
            long k0 = (long)((uint64_t)t2 * (uint64_t)hz / 1000u) * ch; int ok = 0;
            for (int sh = -48; sh <= 48 && !ok; sh++) {                                    /* ms 로 내림한 시각이라 최대 hz/1000 프레임(약 44) 어긋날 수 있음 */
                long k = k0 + (long)sh * ch; int same = k >= 0;
                for (int i = 0; same && i < n * ch && k + i < nrs; i++) { long d = pcm[i] - ref[k + i]; if (d < -1 || d > 1) same = 0; }
                ok = same;
            }
            CHECK(ok, "이동한 자리의 소리가 원본과 다름 (t %u)", t2);
            mp4p_close();
        }
        free(refb);
    } else { printf("usage\n"); return 2; }
    printf(fails ? "avitest: %d FAIL\n" : "avitest: all passed\n", fails);
    return fails != 0;
}
