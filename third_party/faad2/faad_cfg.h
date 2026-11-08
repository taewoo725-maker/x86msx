/* X86MSX 용 faad2 설정 (8.0.93): AAC-LC + SBR·PS (HE-AAC, 8.0.97), 부동소수점, 고정 영역 할당기. */
#ifndef FAAD_CFG_H
#define FAAD_CFG_H
#include <stddef.h>
#define HAVE_STDINT_H 1
#define HAVE_STRING_H 1
#define LC_ONLY_DECODER 1
#define NDEBUG 1
#define PACKAGE_VERSION "2.11.x-x86msx"
double faad_pow(double b, double e);
double faad_sqrt(double x);
double faad_log(double x);
void faad_qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *));
#ifdef FE_UEFI   /* 본체(UEFI): C 라이브러리 없음 — 경고 출력은 버리고 abs 는 직접 */
#define abs(x) ((x) < 0 ? -(x) : (x))
#define fprintf(...) ((void)0)
#define stderr 0
#endif
#endif
