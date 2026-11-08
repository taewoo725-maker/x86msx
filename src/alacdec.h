/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* alacdec.h - Apple Lossless(ALAC) 해독 (8.0.153). third_party/alac = Apple 의 오픈소스 해독기(Apache 2.0)를 그대로 두고 C 에서 부르는 얇은 겉포장 */
#ifndef ALACDEC_H
#define ALACDEC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 마법 쿠키(ALACSpecificConfig 24바이트, 빅엔디안)로 연다. 성공 1. 해독기는 한 번에 하나만 */
int alacdec_open(const uint8_t *cookie, uint32_t len, uint32_t *frame_length, uint32_t *rate, int *channels, int *bits);
/* 패킷 하나를 풀어 16비트 교차(L R L R…) 로 out 에 쓴다 (out 은 frame_length*채널 개). 성공이면 프레임 수, 실패 -1 */
int alacdec_decode(const uint8_t *packet, uint32_t len, int16_t *out);
void alacdec_close(void);
#ifdef __cplusplus
}
#endif
#endif
