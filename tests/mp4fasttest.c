/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
#define _POSIX_C_SOURCE 199309L
/* mp4fasttest - 8.0.123 빠른 화면 처리(Mp4Fast=1)가 기존 방식과 비슷한 그림을 내는지, 얼마나 빠른지.
 * 사용: mp4fasttest <mp4> — SCREEN 5·7·8·12 에서 같은 시각의 그림을 두 방식으로 뽑아 RGB 로 풀어 평균 색 차이를 잰다. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/mp4play.h"
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
static int rd(void *c, uint32_t off, uint8_t *b, uint32_t n) { FILE *f = (FILE *)c; fseek(f, (long)off, SEEK_SET); size_t k = fread(b, 1, n, f); if (k < n) memset(b + k, 0, n - k); return 0; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }
static const uint8_t lv[8] = { 0, 36, 73, 109, 146, 182, 219, 255 };
static void to_rgb(const Mp4Pic *pc, const uint8_t *img, uint8_t *out)       /* 그림 → RGB (w*h*3) */
{
    int W = pc->w, H = pc->h;
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        const uint8_t *row = img + (size_t)y * (size_t)pc->stride; uint8_t *o = out + ((size_t)y * (size_t)W + (size_t)x) * 3;
        if (pc->scr == 8) { uint8_t c = row[x]; o[0] = lv[(c >> 2) & 7]; o[1] = lv[c >> 5]; o[2] = (uint8_t)((c & 3) * 85); }
        else if (pc->scr == 12) {
            int x0 = x & ~3; const uint8_t *g = row + x0; int k = (g[0] & 7) | (g[1] & 7) << 3, j = (g[2] & 7) | (g[3] & 7) << 3;
            if (k > 31) k -= 64; if (j > 31) j -= 64; int yy = row[x] >> 3;
            int r = yy + j, gg = yy + k, b = (5 * yy - 2 * j - k) / 4; r = r < 0 ? 0 : r > 31 ? 31 : r; gg = gg < 0 ? 0 : gg > 31 ? 31 : gg; b = b < 0 ? 0 : b > 31 ? 31 : b;
            o[0] = (uint8_t)(r * 255 / 31); o[1] = (uint8_t)(gg * 255 / 31); o[2] = (uint8_t)(b * 255 / 31);
        } else {
            int code = pc->scr == 6 ? (row[x >> 2] >> (6 - 2 * (x & 3))) & 3 : (x & 1) ? row[x >> 1] & 15 : row[x >> 1] >> 4;
            o[0] = lv[pc->pal[code][0]]; o[1] = lv[pc->pal[code][1]]; o[2] = lv[pc->pal[code][2]];
        }
    }
}
static double blur_diff(const uint8_t *a, const uint8_t *b, int W, int H)     /* 4x4 블록 평균색의 평균 차이 (디더 무늬는 무시하고 색 맞는지) */
{
    double tot = 0; long n = 0;
    for (int by = 0; by + 4 <= H; by += 4) for (int bx = 0; bx + 4 <= W; bx += 4) for (int c = 0; c < 3; c++) {
        int sa = 0, sb = 0;
        for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) { size_t i = (((size_t)(by + y)) * (size_t)W + (size_t)(bx + x)) * 3 + (size_t)c; sa += a[i]; sb += b[i]; }
        tot += abs(sa - sb) / 16.0; n++;
    }
    return tot / (double)n;
}
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); if (!f) { printf("파일 없음\n"); return 1; }
    fseek(f, 0, SEEK_END); uint32_t sz = (uint32_t)ftell(f);
    static uint8_t A[512 * 212 * 3], B[512 * 212 * 3];
    int scrs[] = { 5, 7, 8, 12 };
    for (int s = 0; s < 4; s++) {
        double tm[2] = { 1e9, 1e9 }; double diff = 0; int nf = 0; int got[2] = { 0, 0 };
        for (int rep = 0; rep < 6; rep++) { int fast = rep & 1;
            mp4p_fast = fast;
            int e = mp4p_open(rd, f, sz, scrs[s], 192, 1, 75, 0); CHECK(e == 0, "open S%d fast %d err %d", scrs[s], fast, e); if (e) return 1;
            uint32_t len = mp4p_len_ms(); Mp4Pic pc; double t0 = now(); nf = 0;
            for (uint32_t t = 0; t < len; t += 1000 / 15) { int r = mp4p_frame(t, 0, 0, &pc); nf++;
                if (r > 0 && t >= len / 2 && t < len / 2 + 70) {            /* 가운데 쯤 한 장을 모음 */
                    if (!got[fast]) { got[fast] = 1; to_rgb(&pc, mp4p_image(), fast ? B : A); }
                } }
            { double dt = now() - t0; if (dt < tm[fast]) tm[fast] = dt; } mp4p_close();
        }
        diff = blur_diff(A, B, 256 * (scrs[s] == 7 ? 2 : 1), 192);
        printf("SCREEN %2d  기존 %.2f ms/프레임  빠른 %.2f ms/프레임  (x%.2f)  4x4 평균색 차이 %.1f\n", scrs[s], tm[0] / nf, tm[1] / nf, tm[0] / tm[1], diff);
        CHECK(diff < 40.0, "S%d 색이 너무 다름 %.1f", scrs[s], diff);
        memset(A, 0, sizeof A); memset(B, 0, sizeof B);
    }
    mp4p_fast = 0;
    printf(fails ? "mp4fasttest: %d FAIL\n" : "mp4fasttest: all passed\n", fails);
    return fails != 0;
}
