/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - lwIP(BSD) 플랫폼 정의: 32비트 freestanding 본체용 (kernel/net) */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H
#include <stdint.h>
#include <stddef.h>
#define LWIP_NO_INTTYPES_H 1
#define X8_F  "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "u"
#define LWIP_PLATFORM_DIAG(x)   do {} while (0)
#define LWIP_PLATFORM_ASSERT(x) do { extern void net_assert_fail(const char *); net_assert_fail(x); } while (0)
#define BYTE_ORDER LITTLE_ENDIAN
int atoi(const char *);
#endif
