/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/*
 * libm.c - 본체용 최소 수학 함수와 메모리 풀 (emu2413용)
 * 레거시(32비트)에서는 x87 FPU를 쓰도록 이 파일과 emu2413.c만 따로 컴파일한다.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

double floor(double x)
{
    union { double d; uint64_t u; } v = { x };
    int e = (int)((v.u >> 52) & 0x7FF) - 1023;
    if (e < 0) return x < 0 ? -1.0 : (x == 0 ? x : 0.0);
    if (e >= 52) return x;
    uint64_t frac = ((uint64_t)1 << (52 - e)) - 1;
    if (!(v.u & frac)) return x;
    if (v.u >> 63) v.u += frac;                         /* 음수는 아래쪽으로 */
    v.u &= ~frac;
    return v.d;
}

double round(double x) { return floor(x + 0.5); }

static double reduce(double x)                          /* -π ~ π */
{
    const double tp = 6.283185307179586;
    x -= tp * floor(x / tp);
    if (x > 3.141592653589793) x -= tp;
    return x;
}

double sin(double x)
{
    x = reduce(x);
    double x2 = x * x, t = x, s = x;
    for (int k = 1; k < 12; k++) { t *= -x2 / ((2 * k) * (2 * k + 1)); s += t; }
    return s;
}

double cos(double x) { return sin(x + 1.5707963267948966); }

static uint8_t pool[1536 * 1024] __attribute__((aligned(16)));
static size_t pool_used;

void *malloc(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (pool_used + n > sizeof pool) return 0;
    void *p = pool + pool_used;
    pool_used += n;
    return p;
}

void *calloc(size_t n, size_t m)
{
    void *p = malloc(n * m);
    if (p) memset(p, 0, n * m);
    return p;
}

void free(void *p) { (void)p; }                         /* 칩은 한 번 만들고 계속 씀 */

#ifdef _WIN32
int _fltused = 0;                                       /* UEFI(PE) 링커가 부동소수점 사용 표시로 요구 */
#endif

/* 감마 표 (2.12, 화면 설정): out[i] = 255 x (i/255)^(100/g100) */
static double ln_(double x)                             /* x > 0 */
{
    int k = 0;
    while (x > 1.5) { x *= 0.5; k++; }
    while (x < 0.75) { x *= 2.0; k--; }
    double t = (x - 1.0) / (x + 1.0), t2 = t * t, s = 0, p = t;   /* ln x = 2 atanh(t) */
    for (int n = 1; n < 40; n += 2) { s += p / n; p *= t2; }
    return 2.0 * s + k * 0.6931471805599453;
}
static double exp_(double y)
{
    int k = 0;
    while (y > 0.5) { y -= 0.6931471805599453; k++; }
    while (y < -0.5) { y += 0.6931471805599453; k--; }
    double s = 1, t = 1;
    for (int n = 1; n < 20; n++) { t *= y / n; s += t; }
    while (k > 0) { s *= 2; k--; }
    while (k < 0) { s *= 0.5; k++; }
    return s;
}
void gamma_lut(unsigned char *out, int g100)
{
    if (g100 < 10) g100 = 10;
    double e = 100.0 / g100;
    for (int i = 0; i < 256; i++) {
        double v = i ? 255.0 * exp_(e * ln_(i / 255.0)) : 0;
        int r = (int)(v + 0.5);
        out[i] = (unsigned char)(r < 0 ? 0 : r > 255 ? 255 : r);
    }
}
