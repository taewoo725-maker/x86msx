/* X86MSX 8.0.96: OpenH264 crt_util_safe_x.cpp 대체 (파일·시간 기능 없음, 문자열 함수만) */
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include "crt_util_safe_x.h"
extern "C" {
int32_t WelsVsnprintf(char *buffer, int32_t sizeOfBuffer, const char *format, va_list argptr) {
  if (sizeOfBuffer > 0) buffer[0] = 0;   /* 로그는 쓰지 않음 */
  (void)format; (void)argptr;
  return 0;
}
int32_t WelsSnprintf(char *buffer, int32_t sizeOfBuffer, const char *format, ...) {
  if (sizeOfBuffer > 0) buffer[0] = 0;
  (void)format;
  return 0;
}
char *WelsStrncpy(char *dest, int32_t sizeInBytes, const char *src) {
  if (sizeInBytes <= 0) return dest;
  int32_t i = 0;
  for (; i < sizeInBytes - 1 && src[i]; i++) dest[i] = src[i];
  dest[i] = 0;
  return dest;
}
char *WelsStrcat(char *dest, uint32_t sizeInBytes, const char *src) {
  uint32_t n = 0;
  while (n < sizeInBytes && dest[n]) n++;
  if (n >= sizeInBytes) return dest;
  uint32_t i = 0;
  for (; n + i + 1 < sizeInBytes && src[i]; i++) dest[n + i] = src[i];
  dest[n + i] = 0;
  return dest;
}
WelsFileHandle *WelsFopen(const char *, const char *) { return 0; }
int32_t WelsFclose(WelsFileHandle *) { return 0; }
int32_t WelsFread(void *, int32_t, int32_t, WelsFileHandle *) { return 0; }
int32_t WelsFwrite(const void *, int32_t, int32_t, WelsFileHandle *) { return 0; }
int32_t WelsFseek(WelsFileHandle *, int32_t, int32_t) { return -1; }
int32_t WelsFflush(WelsFileHandle *) { return 0; }
int32_t WelsGetTimeOfDay(SWelsTime *tp) { tp->time = 0; tp->millitm = 0; return 0; }
int32_t WelsStrftime(char *buffer, int32_t size, const char *, const SWelsTime *) { if (size > 0) buffer[0] = 0; return 0; }
uint16_t WelsGetMillisecond(const SWelsTime *tp) { return tp->millitm; }
}
extern "C" double log(double x) {                                  /* 자연로그 (FMO·PSNR 에서만 — 해독 경로에는 거의 안 나옴) */
  if (!(x > 0.0)) return -1e300;
  int e = 0;
  while (x > 1.5) { x *= 0.5; e++; }
  while (x < 0.75) { x *= 2.0; e--; }
  double y = (x - 1.0) / (x + 1.0), y2 = y * y, t = y, s = 0.0;
  for (int k = 1; k < 30; k += 2) { s += t / k; t *= y2; }
  return 2.0 * s + e * 0.6931471805599453;
}
extern "C" double ceil(double x) { long long i = (long long)x; return (double)i < x ? (double)(i + 1) : (double)i; }
