/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_video.inc - 3.2.13 VID-02 영상 녹화 (fe.c가 fe_state.inc 다음에 포함)
 * 파일 → 영상 녹화 (시작/멈춤): MSX 화면 + 소리를 USB Video\X86MSX_0001.avi … 로.
 *   영상 = MJPEG(JPEG 4:4:4, 품질 80), 두 프레임에 한 장(NTSC 약 30, PAL 25 장/초).
 *     크기: 시작할 때 화면이 모두 256폭이면 256 x 192/212, 512폭 줄이 있으면 512 x 384/424 (도중에 바뀌어도 그대로).
 *   소리 = PCM 48 kHz 16비트 모노(소리 저장과 같은 소리), 영상 한 장마다 그 사이 소리 한 덩이(끼워 넣기).
 *   녹화 중에는 메모리(P.vbuf)에만 쌓고, 멈출 때(또는 가득 차면) 한 번에 USB에 씀. 빈도는 소리 샘플 수로 맞춤. */
#include "jpegw.h"

#define VR_IDX_MAX 200000
static uint32_t vrec_n, vrec_frames, vrec_samples, vrec_idx_n, vrec_w, vrec_h, vrec_maxchunk, vrec_tick;
static uint32_t vrec_movi, vrec_sc, vrec_step;   /* 화면 줄임 배수, 몇 번에 한 장 */                           /* 'movi' 글자 자리 (색인 기준) */
typedef struct { uint32_t id, flags, off, size; } VrIdx;
#define VR_IDX_BYTES ((uint32_t)VR_IDX_MAX * 16u)
#define vrec_idx ((VrIdx *)(void *)(P.vbuf + P.vbuf_size - VR_IDX_BYTES))   /* 색인은 버퍼 맨 끝 (BSS를 줄이려고) */
#define VR_LIMIT (P.vbuf_size - VR_IDX_BYTES)
static int16_t vrec_abuf[4096];
static int vrec_an;
#define vrec_rgb (P.scratch + (10u << 20))         /* 화면 RGB 650 KB: 작업 메모리 10 MiB~ (화면 저장·소리 녹음과 안 겹침) */

static void le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void le16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint32_t fourcc(const char *s) { return (uint32_t)s[0] | (uint32_t)s[1] << 8 | (uint32_t)s[2] << 16 | (uint32_t)s[3] << 24; }

#define VR_HDR 512u                                  /* 앞 머리 자리 (hdrl + LIST movi 머리) */

static int vrec_chunk(const char *id, const uint8_t *data, uint32_t len)
{
    uint32_t need = 8 + len + (len & 1);
    if (vrec_n + need + 16u * (vrec_idx_n + 2) + 64 > VR_LIMIT || vrec_idx_n >= VR_IDX_MAX) return -1;
    uint8_t *p = P.vbuf + vrec_n;
    memcpy(p, id, 4); le32(p + 4, len);
    if (data != p + 8) memmove(p + 8, data, len);
    if (len & 1) p[8 + len] = 0;
    vrec_idx[vrec_idx_n].id = fourcc(id); vrec_idx[vrec_idx_n].flags = 0x10;
    vrec_idx[vrec_idx_n].off = vrec_n - vrec_movi; vrec_idx[vrec_idx_n].size = len;
    vrec_idx_n++;
    vrec_n += need;
    if (len > vrec_maxchunk) vrec_maxchunk = len;
    return 0;
}

static void vrec_stop(void);
static int frame_samples(void);
/* play_audio가 프레임마다 (소리 저장과 같은 자리) */
static void vrec_audio(const int16_t *s, int n)
{
    if (!vrec_on || n <= 0) return;
    if (vrec_an + n > (int)(sizeof vrec_abuf / 2)) n = (int)(sizeof vrec_abuf / 2) - vrec_an;
    memcpy(vrec_abuf + vrec_an, s, (size_t)n * 2);
    vrec_an += n;
}

/* 매 프레임 MSX가 돈 뒤: 두 프레임에 한 번 화면을 JPEG로, 쌓인 소리와 함께 */
static void vrec_frame(void)
{
    if (!vrec_on) return;
    if (!D.fb) return;
    if (++vrec_tick % vrec_step) return;
    /* 보이는 화면 전체(메뉴·팝업·진단창 포함)를 vrec_sc분의 1로 줄여 담음. 한 줄은 가로 두 점 평균, 세로는 vrec_sc줄마다 한 줄 (프레임 버퍼는 읽기가 느림) */
    if (!vrec_an) { int n = (int)vrec_step * frame_samples(); if (n > 4096) n = 4096; memset(vrec_abuf, 0, (size_t)n * 2); vrec_an = n; }   /* 창이 열려 MSX가 멈추면 소리가 없음 → 그 시간만큼 무음 (소리 0개인 AVI는 윈도우 플레이어가 못 엽니다) */
    uint32_t row = vrec_w * 3, N = vrec_sc;
    static uint8_t srow[16384], vpal[256 * 3];
    if (D.bpp == 1) {
        if (P.pal_get) P.pal_get(vpal);
        else {
            for (int i = 0; i < 16; i++) memcpy(vpal + i * 3, &mon_rgb[i][0], 3);
            for (int i = 0; i < 210; i++) { vpal[(16 + i) * 3] = (uint8_t)(i / 35 * 255 / 5); vpal[(16 + i) * 3 + 1] = (uint8_t)(i / 5 % 7 * 255 / 6); vpal[(16 + i) * 3 + 2] = (uint8_t)(i % 5 * 255 / 4); }
            for (int i = 0; i < 16; i++) memcpy(vpal + (UI_DAC + i) * 3, ui_rgb[i], 3);
        }
    }
    int ri = D.bgr ? 2 : 0, bi = D.bgr ? 0 : 2;
    for (uint32_t y = 0; y < vrec_h; y++) {
        uint8_t *o = vrec_rgb + y * row;
        memcpy(srow, D.fb + (uintptr_t)(y * N) * D.pitch, D.w * (uint32_t)D.bpp);
        for (uint32_t x = 0; x < vrec_w; x++, o += 3) {
            uint32_t sx = x * N, sx2 = N > 1 ? sx + 1 : sx;
            if (D.bpp == 1) {
                const uint8_t *a = vpal + srow[sx] * 3, *b = vpal + srow[sx2] * 3;
                o[0] = (uint8_t)((a[0] + b[0]) >> 1); o[1] = (uint8_t)((a[1] + b[1]) >> 1); o[2] = (uint8_t)((a[2] + b[2]) >> 1);
            } else {
                const uint8_t *a = srow + sx * 4, *b = srow + sx2 * 4;
                o[0] = (uint8_t)((a[ri] + b[ri]) >> 1); o[1] = (uint8_t)((a[1] + b[1]) >> 1); o[2] = (uint8_t)((a[bi] + b[bi]) >> 1);
            }
        }
    }
    uint8_t *dst = P.vbuf + vrec_n + 8;
    uint32_t used = vrec_n + 8 + 16u * (vrec_idx_n + 2) + 64 + (uint32_t)vrec_an * 2 + 16;
    uint32_t room = VR_LIMIT > used ? VR_LIMIT - used : 0;
    int jl = room > 1024 ? jpeg_encode_rgb(vrec_rgb, (int)vrec_w, (int)vrec_h, (int)row, 80, dst, (int)(room > 0x7FFFFFFF ? 0x7FFFFFFF : room)) : 0;
    if (!jl || vrec_chunk("00dc", dst, (uint32_t)jl) || vrec_chunk("01wb", (const uint8_t *)vrec_abuf, (uint32_t)vrec_an * 2)) { vrec_stop(); return; }
    vrec_frames++; vrec_samples += (uint32_t)vrec_an; vrec_an = 0;
}

static void vrec_start_rec(void)
{
    bgw_finish();                                       /* 5.2.69 */
    if (!P.vbuf || P.vbuf_size < (16u << 20) || !P.scratch || P.scratch_size < (12u << 20)) { toast(T(S_VID_NOMEM), 0); return; }
    if (!P.create || !P.write_file) { toast(T(S_VID_FAIL), 0); return; }
    if (!D.fb || D.w * (uint32_t)D.bpp > 16384u) { toast(T(S_VID_FAIL), 0); return; }
    vrec_sc = (D.w + 959u) / 960u; if (!vrec_sc) vrec_sc = 1;                 /* 가로 960 이하로 줄임 (1920x1080 → 960x536) */
    vrec_w = (D.w / vrec_sc) & ~7u; vrec_h = (D.h / vrec_sc) & ~7u;
    if (vrec_w < 64 || vrec_h < 64 || vrec_w * vrec_h * 3u > (2u << 20)) { toast(T(S_VID_NOMEM), 0); return; }
    vrec_step = vrec_w * vrec_h > 200000u ? 4 : 2;                             /* 큰 화면은 15장/초 (프레임 버퍼 읽기·JPEG 부담) */
    vrec_n = VR_HDR; vrec_movi = VR_HDR - 4;            /* LIST size 'movi' 의 'movi' 자리 */
    vrec_frames = vrec_samples = vrec_idx_n = vrec_maxchunk = vrec_tick = 0; vrec_an = 0;
    vrec_on = 1; vrec_start = P.ms(); margin_bd = -1;
    char t[48] = "";
    num_append(t, sizeof t, P.vbuf_size / (2u << 20) / 60, 0, ' '); str_append(t, sizeof t, lang == 0 ? "분 쯤" : " min");   /* 약 2 MB/초로 어림 */
    toast(T(S_VID_START), t);
}

static void vrec_stop(void)
{
    if (!vrec_on) return;
    vrec_on = 0; margin_bd = -1;
    if (!vrec_frames) { toast(T(S_VID_FAIL), 0); return; }   /* 한 장도 못 담았으면 알림 (전에는 말없이 끝났음) */
    toast(T(S_VID_SAVING), 0);
    if (!P.append) { draw_toast_center(); fe_flush_now(); }   /* 한 번에 쓰는 판(레거시)만 먼저 알림을 보임 */
    uint8_t *b = P.vbuf;
    /* idx1 */
    uint32_t movi_end = vrec_n;
    memcpy(b + vrec_n, "idx1", 4); le32(b + vrec_n + 4, vrec_idx_n * 16);
    for (uint32_t i = 0; i < vrec_idx_n; i++) {
        uint8_t *e = b + vrec_n + 8 + i * 16;
        le32(e, vrec_idx[i].id); le32(e + 4, vrec_idx[i].flags); le32(e + 8, vrec_idx[i].off); le32(e + 12, vrec_idx[i].size);
    }
    uint32_t total = vrec_n + 8 + vrec_idx_n * 16;
    /* 머리 (앞 VR_HDR 바이트 안, 남는 곳은 JUNK) */
    memset(b, 0, VR_HDR);
    uint32_t sr = vrec_samples ? vrec_samples : vrec_frames * 1600u;     /* 소리가 없으면 30장/초로 */
    uint64_t us = (uint64_t)sr * 1000000u / (48000u * (uint64_t)vrec_frames);
    uint8_t *p = b;
    memcpy(p, "RIFF", 4); le32(p + 4, total - 8); memcpy(p + 8, "AVI ", 4);
    p += 12;
    uint8_t *hdrl = p;
    memcpy(p, "LIST", 4); memcpy(p + 8, "hdrl", 4); p += 12;
    memcpy(p, "avih", 4); le32(p + 4, 56);
    le32(p + 8, (uint32_t)us); le32(p + 12, (uint32_t)((uint64_t)(movi_end - VR_HDR) * 1000000u / (us * vrec_frames + 1) + 1)); le32(p + 16, 0);
    le32(p + 20, 0x10 | 0x100); le32(p + 24, vrec_frames); le32(p + 28, 0); le32(p + 32, 2); le32(p + 36, vrec_maxchunk + 8);
    le32(p + 40, vrec_w); le32(p + 44, vrec_h); p += 64;
    /* 영상 */
    uint8_t *sl = p; memcpy(p, "LIST", 4); memcpy(p + 8, "strl", 4); p += 12;
    memcpy(p, "strh", 4); le32(p + 4, 56); memcpy(p + 8, "vids", 4); memcpy(p + 12, "MJPG", 4);
    le32(p + 28, sr); le32(p + 32, vrec_frames * 48000u); le32(p + 40, vrec_frames); le32(p + 44, vrec_maxchunk + 8);
    le32(p + 48, 0xFFFFFFFFu); le16(p + 60, vrec_w); le16(p + 62, vrec_h); p += 64;
    memcpy(p, "strf", 4); le32(p + 4, 40); le32(p + 8, 40); le32(p + 12, vrec_w); le32(p + 16, vrec_h); le16(p + 20, 1); le16(p + 22, 24);
    memcpy(p + 24, "MJPG", 4); le32(p + 28, vrec_w * vrec_h * 3); p += 48;
    le32(sl + 4, (uint32_t)(p - sl - 8));
    /* 소리 */
    sl = p; memcpy(p, "LIST", 4); memcpy(p + 8, "strl", 4); p += 12;
    memcpy(p, "strh", 4); le32(p + 4, 56); memcpy(p + 8, "auds", 4);
    le32(p + 28, 2); le32(p + 32, 96000); le32(p + 40, vrec_samples); le32(p + 44, vrec_maxchunk + 8); le32(p + 48, 0xFFFFFFFFu); le32(p + 52, 2); p += 64;
    memcpy(p, "strf", 4); le32(p + 4, 18); le16(p + 8, 1); le16(p + 10, 1); le32(p + 12, 48000); le32(p + 16, 96000); le16(p + 20, 2); le16(p + 22, 16); le16(p + 24, 0); p += 26;
    le32(sl + 4, (uint32_t)(p - sl - 8));
    le32(hdrl + 4, (uint32_t)(p - hdrl - 8));
    uint32_t junk_at = (uint32_t)(p - b), movi_list = VR_HDR - 12;           /* LIST 머리는 'movi' 앞 8바이트 */
    memcpy(p, "JUNK", 4); le32(p + 4, movi_list - junk_at - 8);
    memcpy(b + movi_list, "LIST", 4); le32(b + movi_list + 4, movi_end - movi_list - 8); memcpy(b + movi_list + 8, "movi", 4);
    char path[160];
    if (next_free_name("Video", "X86MSX_", ".avi", path, sizeof path)) { full_redraw(); toast(T(S_VID_FAIL), 0); return; }
    full_redraw();
    char t[96];
    uint32_t sec = (uint32_t)((uint64_t)sr / 48000u);
    copy_str(t, sizeof t, path_base(path)); str_append(t, sizeof t, " (");
    num_append(t, sizeof t, sec / 60, 0, ' '); str_append(t, sizeof t, ":"); num_append(t, sizeof t, sec % 60, 2, '0'); str_append(t, sizeof t, ")");
    bgw_start(path, b, total, S_VID_SAVED, S_VID_FAIL, t);   /* 조금씩 (영상 버퍼는 다음 녹화·자리 잡기 전까지 그대로) */
}

static void vrec_toggle(void) { if (vrec_on) vrec_stop(); else vrec_start_rec(); }
