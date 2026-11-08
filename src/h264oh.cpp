/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* h264oh.cpp - OpenH264 해독기 껍데기 (src/h264oh.h). 자체 힙 = 주소 순서로 이어 붙인 블록 목록 (첫 맞춤, 뒤 블록과 합침) */
#include <string.h>
#include "h264oh.h"
#include "../third_party/openh264/api/wels/codec_api.h"
#include "../third_party/openh264/common/inc/cpu_core.h"

extern "C" void *mp4h_alloc(size_t n);

OhNew oh_new;
namespace {
struct Blk { size_t size; size_t used; };                  /* 머리 16 바이트 + 본문. size = 본문 크기 */
uint8_t *g_heap; size_t g_size; size_t g_inuse, g_peak;
ISVCDecoder *g_dec;
inline Blk *next_blk(Blk *b) { uint8_t *p = (uint8_t *)(b + 1) + b->size; return p + sizeof(Blk) <= g_heap + g_size ? (Blk *)p : 0; }
}
extern "C" void *oh264_malloc(size_t n)
{
    if (!g_heap) return 0;
    n = (n + 15) & ~(size_t)15; if (!n) n = 16;
    for (Blk *b = (Blk *)g_heap; b; b = next_blk(b)) {
        if (b->used) continue;
        Blk *nx;                                           /* 뒤 빈 블록과 합침 */
        while ((nx = next_blk(b)) && !nx->used) b->size += sizeof(Blk) + nx->size;
        if (b->size < n) continue;
        if (b->size >= n + sizeof(Blk) + 16) {             /* 쪼갬 */
            Blk *rest = (Blk *)((uint8_t *)(b + 1) + n);
            rest->size = b->size - n - sizeof(Blk); rest->used = 0; b->size = n;
        }
        b->used = 1; g_inuse += b->size; if (g_inuse > g_peak) g_peak = g_inuse;
        return b + 1;
    }
    return 0;
}
extern "C" void oh264_free(void *p)
{
    if (!p || !g_heap) return;
    Blk *b = (Blk *)p - 1;
    if (!b->used) return;
    b->used = 0; g_inuse -= b->size;
}
#ifndef FE_UEFI                                            /* 본체 판은 mt32w.cpp 가 정의 */
void operator delete(void *) {}
void operator delete[](void *) {}
void operator delete(void *, size_t) {}
void operator delete[](void *, size_t) {}
extern "C" void __cxa_pure_virtual() { for (;;) {} }
#endif
extern "C" uint32_t oh264_heap_peak(void) { return (uint32_t)g_peak; }

static uint32_t g_cpu_mask;
extern "C" uint32_t oh264_cpu_mask(void) { return g_cpu_mask; }
struct OhWp { uint8_t *p; const uint8_t *q; intptr_t stride; int w, h, logd, w1, w2, off; };   /* src/oh_wp.asm 의 OhWp 와 같은 배치 */
extern "C" void oh_wp_uni(OhWp *a);
extern "C" void oh_wp_bi(OhWp *a);
static inline int wp_ok(int w, int logd) { return (g_cpu_mask & WELS_CPU_SSE2) && (w == 16 || w == 8 || w == 4 || w == 2) && logd >= 0 && logd <= 7; }
extern "C" int oh_wp_uni_run(uint8_t *p, intptr_t stride, int w, int h, int logd, int wt, int off)   /* 1 = 어셈이 처리함, 0 = C 가 해야 함 */
{
    if (!wp_ok(w, logd) || wt < -128 || wt > 128 || off < -128 || off > 127 || h <= 0) return 0;
    OhWp a = { p, 0, stride, w, h, logd, wt, 0, off };
    oh_wp_uni(&a); return 1;
}
extern "C" int oh_wp_bi_run(uint8_t *p, const uint8_t *q, intptr_t stride, int w, int h, int logd, int w1, int w2, int off)
{
    if (!wp_ok(w, logd) || w1 < -32768 || w1 > 32767 || w2 < -32768 || w2 > 32767 || h <= 0) return 0;
    OhWp a = { p, q, stride, w, h, logd, w1, w2, off };
    oh_wp_bi(&a); return 1;
}
extern "C" int oh264_open(size_t heap_bytes)
{
    g_cpu_mask = (WELS_CPU_MMX | WELS_CPU_MMXEXT | WELS_CPU_SSE | WELS_CPU_SSE2 | WELS_CPU_SSE3 | WELS_CPU_SSSE3 | WELS_CPU_CMOV | WELS_CPU_FPU);   /* AVX 이상은 UEFI 에서 켜져 있는지 몰라 뺌 */
    g_heap = (uint8_t *)mp4h_alloc(heap_bytes);
    if (!g_heap) return -1;
    g_size = heap_bytes; g_inuse = g_peak = 0; g_dec = 0;
    Blk *b = (Blk *)g_heap; b->size = g_size - sizeof(Blk); b->used = 0;
    if (WelsCreateDecoder(&g_dec) != 0 || !g_dec) { g_dec = 0; return -2; }
    SDecodingParam p; memset(&p, 0, sizeof p);
    p.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    p.eEcActiveIdc = ERROR_CON_DISABLE;
    int th = 1; g_dec->SetOption(DECODER_OPTION_NUM_OF_THREADS, &th);
    if (g_dec->Initialize(&p) != 0) { WelsDestroyDecoder(g_dec); g_dec = 0; return -3; }
    return 0;
}
extern "C" void oh264_close(void)
{
    if (g_dec) { g_dec->Uninitialize(); WelsDestroyDecoder(g_dec); g_dec = 0; }
    g_heap = 0; g_size = 0;
}
static int take(const unsigned char *dst[3], const SBufferInfo &info, Oh264Pic *pic)
{
    if (info.iBufferStatus != 1 || !dst[0]) return 0;
    pic->y = dst[0]; pic->u = dst[1]; pic->v = dst[2];
    pic->w = info.UsrData.sSystemBuffer.iWidth; pic->h = info.UsrData.sSystemBuffer.iHeight;
    pic->sy = info.UsrData.sSystemBuffer.iStride[0]; pic->suv = info.UsrData.sSystemBuffer.iStride[1];
    pic->ts = (uint32_t)info.uiOutYuvTimeStamp;
    return 1;
}
extern "C" int oh264_decode(const uint8_t *nal, int len, uint32_t ts, Oh264Pic *pic)
{
    if (!g_dec) return -1;
    unsigned char *dst[3] = { 0, 0, 0 };
    SBufferInfo info; memset(&info, 0, sizeof info);
    info.uiInBsTimeStamp = ts;
    DECODING_STATE st = g_dec->DecodeFrameNoDelay(nal, len, dst, &info);
    if (take((const unsigned char **)dst, info, pic)) return 1;
    return (st & (dsNoParamSets | dsOutOfMemory | dsInvalidArgument)) ? -1 : 0;
}
extern "C" int oh264_flush(Oh264Pic *pic)
{
    if (!g_dec) return 0;
    unsigned char *dst[3] = { 0, 0, 0 };
    SBufferInfo info; memset(&info, 0, sizeof info);
    g_dec->FlushFrame(dst, &info);
    return take((const unsigned char **)dst, info, pic);
}
