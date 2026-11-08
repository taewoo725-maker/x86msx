/* X86MSX: 운영체제 없음 — OpenH264 가 쓰는 시간 자료형만 */
#ifndef X86_TIME_SHIM_H
#define X86_TIME_SHIM_H
typedef long long time_t;
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
#endif
