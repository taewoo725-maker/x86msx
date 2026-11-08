/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* mp4play_avi.inc - AVI (MJPEG 영상 + PCM 소리) 재생 부품 (8.0.121)
 * mp4play.c 안에 #include 되며, MP4 쪽은 분기 한 줄씩(avi_*)으로만 연결한다.
 * 이 파일을 지워도 MP4 재생은 그대로다. (레고 원칙) */
#include "avi.h"
#include "jpegdec.h"

static uint32_t a_cum[MP4A_MAXS];                           /* PCM: 조각 시작까지의 누적 바이트 */
static AviInfo AI;

static int avi_decode(uint32_t i, uint32_t sz)               /* 한 조각 = JPEG 한 장 (s_buf 에 읽어 둠) */
{
        int w, h;
        if (jpg_decode(s_buf, sz, P.ybuf, P.ybuf_n, &w, &h)) return 0;       /* 깨진 장은 건너뜀 */
        P.mbw = (uint32_t)(w + 1) & ~1u; P.mbh = (uint32_t)(h + 1) & ~1u; P.cl = P.ct = 0; P.cw = (uint32_t)w; P.ch = (uint32_t)h;
        P.yuv = P.ybuf; P.have_pic = 1; P.pic_sample = i;
        return 1;
}

static int avi_audio_open(void)
{
        if (!AI.has_audio || AI.atag != 1 || (AI.abits != 8 && AI.abits != 16) || AI.ach < 1 || AI.ach > 2 || AI.arate < 4000 || AI.arate > 96000 || AI.na == 0) return 0;
        A.pcm = 1; A.bits = AI.abits; A.ch = AI.ach; A.rate = AI.arate; A.ba = AI.ach * (AI.abits / 8); A.n = AI.na;
        uint32_t cum = 0; for (uint32_t i = 0; i < A.n; i++) { a_cum[i] = cum; cum += a_size[i]; }
        A.open = 1; A.cur = 0; A.pi = A.po = 0;
        return 1;
}

static int avi_audio_next(const int16_t **pcm, int *frames, int *ch, int *hz)
{
        static int16_t po_[4096 * 2];
        while (A.pi < A.n) {
            uint32_t sz = a_size[A.pi];
            if (A.po >= sz) { A.pi++; A.po = 0; continue; }
            uint32_t fr = (sz - A.po) / (uint32_t)A.ba;
            if (!fr) { A.pi++; A.po = 0; continue; }
            if (fr > 4096) fr = 4096;
            uint32_t nb = fr * (uint32_t)A.ba;
            if (rdb(a_off[A.pi] + A.po, a_buf, nb)) { A.pi++; A.po = 0; continue; }
            A.po += nb;
            uint32_t ns = fr * (uint32_t)A.ch;
            if (A.bits == 16) for (uint32_t k = 0; k < ns; k++) po_[k] = (int16_t)(a_buf[2 * k] | a_buf[2 * k + 1] << 8);
            else for (uint32_t k = 0; k < ns; k++) po_[k] = (int16_t)(((int)a_buf[k] - 128) << 8);
            *pcm = po_; *frames = (int)fr; *ch = A.ch; *hz = (int)A.rate; A.frames++;
            return 1;
        }
        return 0;
    return 0;
}

static uint32_t avi_audio_seek(uint32_t t_ms)
{
        uint64_t b = (uint64_t)t_ms * A.rate / 1000u * (uint64_t)A.ba; uint32_t i = 0;
        while (i + 1 < A.n && a_cum[i + 1] <= b) i++;
        A.pi = i; A.po = (uint32_t)(b - a_cum[i]); A.po -= A.po % (uint32_t)A.ba;
        if (A.po > a_size[i]) A.po = 0;
        return (uint32_t)((uint64_t)(a_cum[i] + A.po) / (uint32_t)A.ba * 1000u / A.rate);
}

static int avi_open(Mp4Read rd, void *ctx, uint32_t fsize, int scr, int w, int h, int st, int np, int fit, int dither, int maxfps)
{
    int e;
        if (P.mi.width > JPG_MAXW || P.mi.height > JPG_MAXH) return MP4E_PROFILE;
        AviTabs t = { s_off, s_size, s_key, MP4P_MAXS, a_off, a_size, MP4A_MAXS };
        memset(s_key, 0, MP4P_MAXS / 8);
    e = avi_parse(rd, ctx, fsize, &AI, &t);
        if (e != MP4E_NONE) { memset(&P, 0, sizeof P); return e; }
        P.n = AI.nv; memset(s_key, 0xFF, (P.n + 7) / 8);        /* MJPEG 는 모든 장이 키 */
        for (uint32_t i = 0; i < P.n; i++) s_dts[i] = (uint32_t)((uint64_t)i * 1000u * AI.scale / AI.rate);
        P.dur_ms = (uint32_t)((uint64_t)P.n * 1000u * AI.scale / AI.rate);
        P.scr = scr; P.lines = h; P.fit = fit; P.dither = dither < 0 ? 0 : dither > 100 ? 100 : dither; P.maxfps = maxfps < 0 ? 0 : maxfps;
        P.pic.scr = scr; P.pic.w = w; P.pic.h = h; P.pic.stride = st; P.pic.npal = np; P.npal = np; P.pic.bytes = (uint32_t)(st * h);
        P.mj = 1; P.ybuf_n = JPG_OUT_BYTES; P.ybuf = (uint8_t *)mp4h_alloc(P.ybuf_n);
        if (!P.ybuf) { arena_used = 0; memset(&P, 0, sizeof P); return MP4E_PROFILE; }
        P.open = 1; P.cur = 0;
        int r = decode_sample(0);
        for (uint32_t k = 1; r <= 0 && k < P.n && k < 8; k++) r = decode_sample(k);
        if (r <= 0 || !P.have_pic) { mp4p_close(); return MP4E_PROFILE; }
        P.cur = 1; P.dropped = 0; P.shown_ms = 0; P.ended = 0;
        P.mi.width = (uint16_t)P.cw; P.mi.height = (uint16_t)P.ch;
        return MP4E_NONE;
}
