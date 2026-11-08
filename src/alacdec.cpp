/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* alacdec.cpp - ALAC 해독 겉포장 (8.0.153). 해독기 본체는 third_party/alac (Apple, Apache 2.0) */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "alacdec.h"
#include "../third_party/alac/ALACDecoder.h"
#include "../third_party/alac/ALACBitUtilities.h"
struct AlacPlace {};
inline void *operator new(size_t, AlacPlace &, void *where) { return where; }
static ALACDecoder *dec;
alignas(16) static unsigned char dec_mem[sizeof(ALACDecoder) + 16];
static uint32_t frame_len; static int nch, nbits;
static uint8_t *raw;                                           /* 해독기가 내는 원본 바이트 (frame_length * 채널 * 4) */

extern "C" int alacdec_open(const uint8_t *cookie, uint32_t len, uint32_t *frame_length, uint32_t *rate, int *channels, int *bits)
{
    alacdec_close();
    if (len < 24) return 0;
    uint8_t cfg[24]; memcpy(cfg, cookie, 24);
    dec = reinterpret_cast<ALACDecoder *>(dec_mem);
    AlacPlace pl; new (pl, dec_mem) ALACDecoder();
    if (dec->Init(cfg, 24) != 0) { dec->~ALACDecoder(); dec = 0; return 0; }
    frame_len = dec->mConfig.frameLength; nch = dec->mConfig.numChannels; nbits = dec->mConfig.bitDepth;
    if (frame_len < 64 || frame_len > 16384 || nch < 1 || nch > 2 || (nbits != 16 && nbits != 24 && nbits != 32) || dec->mConfig.sampleRate < 8000 || dec->mConfig.sampleRate > 192000) { dec->~ALACDecoder(); dec = 0; return 0; }
    raw = (uint8_t *)malloc((size_t)frame_len * (size_t)nch * 4u);
    if (!raw) { dec->~ALACDecoder(); dec = 0; return 0; }
    *frame_length = frame_len; *rate = dec->mConfig.sampleRate; *channels = nch; *bits = nbits;
    return 1;
}

extern "C" int alacdec_decode(const uint8_t *packet, uint32_t len, int16_t *out)
{
    if (!dec || !raw || len == 0) return -1;
    BitBuffer bb; BitBufferInit(&bb, const_cast<uint8_t *>(packet), len);
    uint32_t got = 0;
    if (dec->Decode(&bb, raw, frame_len, (uint32_t)nch, &got) != 0 || got == 0 || got > frame_len) return -1;
    uint32_t n = got * (uint32_t)nch;
    if (nbits == 16) { for (uint32_t i = 0; i < n; i++) out[i] = (int16_t)(raw[2 * i] | raw[2 * i + 1] << 8); }
    else if (nbits == 24) { for (uint32_t i = 0; i < n; i++) out[i] = (int16_t)(raw[3 * i + 1] | raw[3 * i + 2] << 8); }
    else { for (uint32_t i = 0; i < n; i++) out[i] = (int16_t)(raw[4 * i + 2] | raw[4 * i + 3] << 8); }
    return (int)got;
}

extern "C" void alacdec_close(void)
{
    if (dec) { dec->~ALACDecoder(); dec = 0; }
    if (raw) { free(raw); raw = 0; }
}
