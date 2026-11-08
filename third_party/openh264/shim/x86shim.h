/* X86MSX 8.0.96: OpenH264 를 운영체제 없이 빌드하려고 모든 소스에 먼저 넣는 머리글 */
#ifndef X86_OH264_SHIM_H
#define X86_OH264_SHIM_H
#include <stdint.h>
#include <stddef.h>
extern "C" { int abs(int x); double log(double x); double ceil(double x); void *oh264_malloc(size_t n); void oh264_free(void *p); }
struct OhNew { int unused; };
extern OhNew oh_new;
inline void *operator new(size_t n, OhNew &) { return oh264_malloc(n); }
inline void *operator new[](size_t n, OhNew &) { return oh264_malloc(n); }
#endif
