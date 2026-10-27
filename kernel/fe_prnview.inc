/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* fe_prnview.inc - 4.0.5 인쇄 미리보기
 *   인쇄가 끝나(파일로 저장되면) 인쇄된 용지를 팝업(창 16)으로 보여 줌. 옛 연속 용지(양옆에 트랙터 구멍)처럼.
 *   받은 바이트(prn_buf)를 도트 프린터(Epson FX 호환: MSX 프린터) 명령으로 풀어 960 x 792 점 쪽 그림으로 그림.
 *   가로 120 dpi(8 inch), 세로 72 dpi(11 inch). 글자(6x11 점 글꼴), 그림 명령 ESC K L Y Z *, 줄 간격 ESC A 2 0 1 3 J, 쪽 넘김 FF·넘침.
 *   쪽 그림은 한 쪽씩 필요할 때 다시 그림 (쪽 시작 위치는 한 번 훑어 모음). */
#include "prnfont.h"

#define PV_W 1080                                       /* 인쇄 폭 9 inch x 120 dpi (그림 512열 x 60 dpi = 1024점이 들어가게) */
#define PV_TW 960                                       /* 글자는 8 inch(80자)에서 줄 바꿈 */
#define PV_H 792                                        /* 길이 11 inch x 72 dpi */
#define PV_STRIP 90                                     /* 양옆 구멍 띠 0.75 inch */
#define PV_PW (PV_W + 2 * PV_STRIP)
#define PV_TALL (PV_H * 5)                                /* 이어 붙인 용지 (최대 5쪽) 높이 (점) */
#define PV_KEEP 5                                        /* 미리보기에 두는 쪽 수 (가장 나중 5쪽) */
static uint8_t (*pv_bits)[PV_W / 8];                    /* P.scratch 앞쪽 (커널 BSS가 모자라 정적 배열을 못 둠). 그릴 때마다 다시 풀어 채움 */
#define PV_BYTES (PV_H * (PV_W / 8))
#define PV_TBYTES (PV_TALL * (PV_W / 8))
static int pv_base, pv_npages, pv_page, pv_zoom, pv_scx, pv_scy;
static int pv_ht, pv_head, pv_yb, pv_follow, pv_ah, pv_dh, pv_k, pv_scmax;      /* 4.0.6 이어 붙인 용지: 전체 높이·인쇄 머리 위치·쪽 시작 높이 */
static int pv_ph[PV_KEEP + 2];                        /* 쪽마다 길이 (점) */
static uint8_t pv_rowtear[PV_TALL];                   /* 뜯는 줄 가까운 줄 */
static const uint8_t *pv_data;
static uint32_t pv_len;
static char pv_name[48];

static void int_to_str(char *o, int max, int v)
{
    char t[12]; int n = 0;
    if (v <= 0) t[n++] = '0';
    while (v > 0 && n < 11) { t[n++] = (char)('0' + v % 10); v /= 10; }
    int k = 0;
    while (n && k < max - 1) o[k++] = t[--n];
    o[k] = 0;
}
static int pv_ink;
static void pv_dot(int draw, int x, int y)
{
    y += pv_yb;                                         /* 4.0.7: 이어 붙인 용지 안의 높이 */
    if (x < 0 || x >= PV_W || y < 0) return;
    pv_ink = 1;
    if (draw && y < PV_TALL) pv_bits[y][x >> 3] |= (uint8_t)(0x80 >> (x & 7));
}


/* 4.0.6: MSX 문자표 (0x80~0xFF 가타카나·그래픽, 0x01 xx 그래픽 기호) — 지금 기종 BIOS의 글꼴표(CGTBL: 번지가 0004h에 있음, 8x8)를 읽어 둠.
 * 상용 글꼴 파일은 따로 싣지 않고 USB의 그 기종 ROM이 읽은 것을 그대로 씀. BIOS가 보일 때(페이지 0이 ROM일 때) 한 번 읽음 */
static uint8_t pv_cg[2048];
static int pv_cg_ok;
static void pv_grab_font(void)
{
    if (pv_cg_ok || !M || msx_peek(M, 0) != 0xF3) return;
    uint16_t a = (uint16_t)(msx_peek(M, 4) | (msx_peek(M, 5) << 8));
    if (a < 0x0100 || a > 0x3800) return;
    int any = 0;
    for (int i = 0; i < 2048; i++) { pv_cg[i] = msx_peek(M, (uint16_t)(a + i)); if (i >= 0x41 * 8 && i < 0x42 * 8 && pv_cg[i]) any = 1; }
    pv_cg_ok = any;
}
static void pv_cg_glyph(int draw, int x, int y, int code, int wd, int bold)
{
    if (!pv_cg_ok) return;
    const uint8_t *g = pv_cg + (code & 255) * 8;
    for (int r = 0; r < 9; r++) { uint8_t b = g[r * 8 / 9]; for (int gx = 0; gx < wd; gx++) if (b & (0x80 >> (gx * 8 / wd))) { pv_dot(draw, x + gx, y + 1 + r); if (bold) pv_dot(draw, x + gx + 1, y + 1 + r); } }   /* 8x8 → 12x9 점, 한 줄 아래 (영문 글꼴 기준선에 맞춤) */
}

/* 4.0.9: 한글 모아쓰기. 한국 MSX는 한글을 자모 단위 1바이트로 보냄 (ㄱ=86h … ㅎ=98h, ㅏ=99h … ㅣ=A6h). 화면(BIOS)은 이를 모아 16x16으로 그림.
 * 미리보기는 같은 규칙(초성+중성(+겹모음)+종성(+겹받침))으로 묶어, 기종 BIOS의 8x8 자모 글꼴을 늘이고 줄여 한 글자(두 칸 폭, 두 줄 높이)로 그림. H 키로 풀어쓰기(바이트 그대로)와 전환 */
static int pv_korean, pv_hcompose = 1;
static int pv_hc(uint8_t c) { return c >= 0x86 && c <= 0x98; }
static int pv_hv(uint8_t c) { return c >= 0x99 && c <= 0xA6; }
static int pv_hfinalok(uint8_t c) { return pv_hc(c) && c != 0x8A && c != 0x8E && c != 0x93; }        /* ㄸ ㅃ ㅉ은 받침이 못 됨 */
static int pv_hhor(uint8_t c) { return c == 0xA1 || c == 0xA2 || c == 0xA3 || c == 0xA4 || c == 0xA5; }   /* ㅗ ㅛ ㅜ ㅠ ㅡ: 초성 아래 가로로 */
static int pv_hvcomp(uint8_t a, uint8_t b) { return (a == 0xA1 && (b == 0x99 || b == 0x9A || b == 0xA6)) || (a == 0xA3 && (b == 0x9D || b == 0x9E || b == 0xA6)) || (a == 0xA5 && b == 0xA6); }
static int pv_hfcomp(uint8_t a, uint8_t b)
{
    static const uint8_t t[11][2] = { {0x86,0x8F},{0x88,0x92},{0x88,0x98},{0x8B,0x86},{0x8B,0x8C},{0x8B,0x8D},{0x8B,0x8F},{0x8B,0x96},{0x8B,0x97},{0x8B,0x98},{0x8D,0x8F} };
    for (int k = 0; k < 11; k++) if (t[k][0] == a && t[k][1] == b) return 1;
    return 0;
}
/* 8x8 자모 글꼴을 (x0,y0,w,h) 칸에 맞춰 (줄이면 겹치는 줄·열을 합쳐 가는 획이 빠지지 않게) */
static void pv_cgbox(int draw, int code, int x0, int y0, int w, int h, int bold)
{
    if (!pv_cg_ok || w < 1 || h < 1) return;
    const uint8_t *g = pv_cg + (code & 255) * 8;
    for (int dy = 0; dy < h; dy++) {
        int r0 = dy * 8 / h, r1 = (dy + 1) * 8 / h;
        if (r1 <= r0) r1 = r0 + 1;
        uint8_t rows = 0;
        for (int r = r0; r < r1 && r < 8; r++) rows |= g[r];
        for (int dx = 0; dx < w; dx++) {
            int c0 = dx * 8 / w, c1 = (dx + 1) * 8 / w;
            if (c1 <= c0) c1 = c0 + 1;
            int on = 0;
            for (int c = c0; c < c1 && c < 8; c++) if (rows & (0x80 >> c)) on = 1;
            if (on) { pv_dot(draw, x0 + dx, y0 + dy); if (bold) pv_dot(draw, x0 + dx + 1, y0 + dy); }
        }
    }
}
/* 글자 하나 (너비 2칸 = 2*wd, 높이 22점) */
static void pv_syllable(int draw, int x, int y, uint8_t ini, uint8_t v1, uint8_t v2, uint8_t f1, uint8_t f2, int wd, int bold)
{
    int hw = wd, H = 22, fin = f1 != 0;
    if (!pv_hhor(v1)) {                                 /* ㅏ ㅐ ㅑ ㅓ ㅔ ㅕ ㅣ …: 초성 왼쪽 위, 모음 오른쪽 (길게), 받침 아래 */
        int vh = f2 ? 11 : H;
        if (ini) pv_cgbox(draw, ini, x, y, hw, fin ? 11 : 11, bold);
        pv_cgbox(draw, v1, x + hw - 3, y, hw, vh, bold);
        if (f1) pv_cgbox(draw, f1, x, y + 11, f2 ? hw : hw, 11, bold);
        if (f2) pv_cgbox(draw, f2, x + hw, y + 11, hw, 11, bold);
    } else {                                            /* ㅗ ㅜ ㅡ …: 초성 위, 모음 가로로 가운데, 받침 아래 (겹모음이면 뒤 모음이 오른쪽에 길게) */
        int top = fin ? 7 : 10, mh = fin ? 6 : 10, by = y + top + mh;
        if (ini) pv_cgbox(draw, ini, x + hw / 2, y, hw, top, bold);
        if (v2) { pv_cgbox(draw, v1, x, y + top, hw, mh, bold); pv_cgbox(draw, v2, x + hw, y, hw, top + mh + 2, bold); }
        else pv_cgbox(draw, v1, x, y + top, 2 * hw, mh, bold);
        if (f1) pv_cgbox(draw, f1, x, by + 1, hw, H - top - mh - 1, bold);
        if (f2) pv_cgbox(draw, f2, x + hw, by + 1, hw, H - top - mh - 1, bold);
    }
}

/* target < 0: 쪽 수만 셈 (pv_total). 그 밖: 그 쪽을 pv_bits에 그림.
 * 옛 Epson FX·MX 계열(MSX 프린터) 명령: 이송 LF CR FF VT HT BS, ESC B/D(세로·가로 탭) C(쪽 길이) N/O(접는 줄 건너뛰기) 4(맨 위 다시),
 * 줄 간격 ESC 0 1 2 3 A J, 글자 SI DC2 SO DC4 ESC W M P E F G H - S T(위·아래 첨자), 그림 ESC K L Y Z * (NEC ESC S nnnn·ESC T nn 포함) */
static int pv_total;
static void pv_run(int target)
{
    int page = 0, x = 0, y = 0, ls = 12, lsp = 12, cw = 12, dwid = 0, pl = PV_H, skip = 0;
    int bold = 0, uline = 0, sup = 0, cum = 0, hl = 0, hmode = pv_korean && pv_hcompose && pv_cg_ok;
    int vt[16], nvt = 0, ht[32], nht = 0;
    uint32_t i = 0;
    pv_ink = 0; pv_yb = 0; pv_head = 0;
    if (target < 0) pv_total = 1;
    else memset(pv_bits, 0, PV_TBYTES);
#define PV_NEWPAGE() do { if (target >= 0 && page >= pv_base) { if (page - pv_base < PV_KEEP + 1) pv_ph[page - pv_base] = pl; cum += pl; } \
        page++; y = 0; pv_yb = cum; if (target < 0) { pv_total = page + 1; } pv_ink = 0; } while (0)
#define PV_ADV(d) do { y += (d); if (y >= pl || (skip && y + skip >= pl)) PV_NEWPAGE(); } while (0)
    while (i < pv_len) {
        int draw = target >= 0 && page >= pv_base;
        uint8_t c = pv_data[i++];
        if (c == 0x1B) {
            if (i >= pv_len) break;
            uint8_t e = pv_data[i++];
            if (e == 'K' || e == 'L' || e == 'Y' || e == 'Z' || e == '*') {
                int m = 0, dpi = 60, bpc = 1;
                if (e == '*') { if (i >= pv_len) break; m = pv_data[i++]; }
                if (i + 1 >= pv_len) break;
                uint32_t n = (uint32_t)pv_data[i] | ((uint32_t)pv_data[i + 1] << 8);
                i += 2;
                if (e == 'K') dpi = 60; else if (e == 'L' || e == 'Y') dpi = 120; else if (e == 'Z') dpi = 240;
                else if (m < 8) { static const uint16_t t[8] = { 60, 120, 120, 240, 80, 72, 90, 144 }; dpi = t[m]; }
                else { bpc = 3; dpi = m == 32 ? 60 : m == 33 ? 120 : m == 38 ? 90 : m == 39 ? 180 : 360; }
                for (uint32_t col = 0; col < n && i + (uint32_t)bpc <= pv_len; col++, i += (uint32_t)bpc) {
                    int px = x + (int)(col * 120u / (uint32_t)dpi);
                    if (bpc == 1) { uint8_t b = pv_data[i]; for (int k = 0; k < 8; k++) if (b & (0x80 >> k)) pv_dot(draw, px, y + k); }
                    else for (int k = 0; k < 24; k++) if (pv_data[i + (uint32_t)(k >> 3)] & (0x80 >> (k & 7))) pv_dot(draw, px, y + k * 2 / 5);
                }
                if (i > pv_len) i = pv_len;
                x += (int)(n * 120u / (uint32_t)dpi);
                continue;
            }
            if ((e == 'S' || e == 'T') && i + (e == 'S' ? 3u : 1u) < pv_len) {   /* NEC·MSX 프린터: ESC S nnnn = 8점 그림 (60 dpi), ESC T nn = 줄 간격 nn/144 inch */
                int nd = e == 'S' ? 4 : 2, ok = 1, v = 0;
                for (int q = 0; q < nd; q++) { uint8_t ch = pv_data[i + (uint32_t)q]; if (ch < '0' || ch > '9') ok = 0; else v = v * 10 + (ch - '0'); }
                if (ok) {
                    i += (uint32_t)nd;
                    if (e == 'T') { ls = v / 2 < 1 ? 1 : v / 2; continue; }
                    for (int col = 0; col < v && i < pv_len; col++, i++) {
                        uint8_t b = pv_data[i];
                        for (int k = 0; k < 8; k++) if (b & (0x80 >> k)) { pv_dot(draw, x + col * 2, y + k); pv_dot(draw, x + col * 2 + 1, y + k); }
                    }
                    x += v * 2;
                    continue;
                }
            }
            if (e == 'V' && i + 7 <= pv_len) {              /* MSX 컬러 프린터의 ESC V (3자리 + 4자리 + 3자리 x 8바이트): 읽어 넘김 */
                int ok = 1, v = 0;
                for (int q = 0; q < 7; q++) { uint8_t ch = pv_data[i + (uint32_t)q]; if (ch < '0' || ch > '9') ok = 0; else if (q < 3) v = v * 10 + (ch - '0'); }
                if (ok) { i += 7u + (uint32_t)v * 8u; if (i > pv_len) i = pv_len; continue; }
            }
            switch (e) {
            case '@': ls = lsp = 12; cw = 12; dwid = 0; pl = PV_H; skip = 0; bold = uline = sup = 0; nvt = nht = 0; break;
            case 'A': if (i < pv_len) lsp = pv_data[i++]; if (lsp < 1) lsp = 1; break;
            case '2': ls = lsp; break;
            case '0': ls = 9; break;
            case '1': ls = 7; break;
            case '3': if (i < pv_len) { ls = pv_data[i++] / 3; if (ls < 1) ls = 1; } break;
            case 'J': if (i < pv_len) { PV_ADV(pv_data[i++] / 3); } break;
            case 'M': cw = 10; break;                                  /* 엘리트 12 cpi */
            case 'P': cw = 12; break;
            case 'W': if (i < pv_len) dwid = pv_data[i++] & 1; break;
            case 'E': case 'G': bold = 1; break;                        /* 굵게·이중 타격 */
            case 'F': case 'H': bold = 0; break;
            case '-': if (i < pv_len) uline = pv_data[i++] & 1; break;
            case 'S': if (i < pv_len) { uint8_t v = pv_data[i++]; sup = (v & 1) ? 2 : 1; } break;   /* 위(0)·아래(1) 첨자 */
            case 'T': sup = 0; break;
            case 'C': if (i < pv_len) {                                /* 쪽 길이: n줄 (n = 0이면 다음 바이트가 inch) */
                int n = pv_data[i++];
                if (n == 0 && i < pv_len) pl = pv_data[i++] * 72; else pl = n * ls;
                if (pl < ls) pl = ls;
                if (pl > PV_H) pl = PV_H;
                skip = 0;
            } break;
            case 'N': if (i < pv_len) { skip = pv_data[i++] * ls; if (skip >= pl) skip = 0; } break;   /* 쪽 아래 n줄 건너뛰기 */
            case 'O': skip = 0; break;
            case '4': y = 0; break;                                    /* 지금 위치를 쪽 맨 위로 */
            case 'B': nvt = 0; while (i < pv_len) { uint8_t v = pv_data[i++]; if (!v) break; if (nvt < 16) vt[nvt++] = v * ls; } break;   /* 세로 탭 (줄 번호) */
            case 'D': nht = 0; while (i < pv_len) { uint8_t v = pv_data[i++]; if (!v) break; if (nht < 32) ht[nht++] = v; } break;   /* 가로 탭 (칸 번호) */
            default: {
                static const char one[] = "AQRacilnoprsxy!/=%+&";   /* 인자가 하나 붙는 명령 (읽고 버림) */
                for (const char *q = one; *q; q++) if (*q == (char)e) { if (i < pv_len) i++; break; }
            }
            }
            continue;
        }
        if (hmode && (pv_hv(c) || (pv_hc(c) && i < pv_len && pv_hv(pv_data[i])))) {      /* 한글 한 글자 묶기 */
            uint32_t p = i - 1;
            uint8_t ini = 0, v1, v2 = 0, f1 = 0, f2 = 0;
            if (pv_hc(pv_data[p])) { ini = pv_data[p]; p++; }
            v1 = pv_data[p++];
            if (p < pv_len && pv_hv(pv_data[p]) && pv_hvcomp(v1, pv_data[p])) v2 = pv_data[p++];
            if (p < pv_len && pv_hfinalok(pv_data[p]) && !(p + 1 < pv_len && pv_hv(pv_data[p + 1]))) {
                f1 = pv_data[p++];
                if (p < pv_len && pv_hfinalok(pv_data[p]) && pv_hfcomp(f1, pv_data[p]) && !(p + 1 < pv_len && pv_hv(pv_data[p + 1]))) f2 = pv_data[p++];
            }
            int wd = cw * (dwid ? 2 : 1);
            if (x + 2 * wd > PV_TW) { x = 0; PV_ADV(ls + (hl ? 10 : 0)); hl = 0; draw = target >= 0 && page >= pv_base; }
            pv_syllable(draw, x, y, ini, v1, v2, f1, f2, wd, 1);
            x += 2 * wd; hl = 1; i = p;
            continue;
        }
        switch (c) {
        case 0x0D: x = 0; break;
        case 0x0A: PV_ADV(ls + (hl ? 10 : 0)); hl = 0; break;
        case 0x0B: {                                        /* 세로 탭: 정한 줄이 없으면 LF처럼 */
            int to = -1;
            for (int q = 0; q < nvt; q++) if (vt[q] > y) { to = vt[q]; break; }
            if (to < 0) { if (nvt) PV_NEWPAGE(); else PV_ADV(ls); } else PV_ADV(to - y);
            break; }
        case 0x0C: PV_NEWPAGE(); x = 0; break;
        case 0x08: x -= cw * (dwid ? 2 : 1); if (x < 0) x = 0; break;
        case 0x09: {
            int wd = cw * (dwid ? 2 : 1), nx = -1;
            if (nht) { for (int q = 0; q < nht; q++) if (ht[q] * wd > x) { nx = ht[q] * wd; break; } if (nx < 0) break; }   /* 탭을 지정했으면 넘는 탭은 무시 */
            else nx = (x / (wd * 8) + 1) * wd * 8;
            x = nx; break; }
        case 0x01:                                          /* 0x01 xx: 그래픽 기호 (xx = 0x40 + 기호 번호) */
            if (i < pv_len) {
                int cc = pv_data[i++] - 0x40, wd = cw * (dwid ? 2 : 1);
                if (cc >= 0 && cc < 32) {
                    if (x + wd > PV_TW) { x = 0; PV_ADV(ls); draw = target >= 0 && page >= pv_base; }
                    pv_cg_glyph(draw, x, y, cc, wd, bold);
                    x += wd;
                }
            }
            break;
        case 0x0E: dwid = 1; break;
        case 0x14: dwid = 0; break;
        case 0x0F: cw = 7; break;
        case 0x12: cw = 12; break;
        default:
            if (c >= 0x20 && c <= 0x7E) {
                int wd = cw * (dwid ? 2 : 1), gw = wd / 6 < 1 ? 1 : wd / 6;
                if (x + wd > PV_TW) { x = 0; PV_ADV(ls + (hl ? 10 : 0)); hl = 0; draw = target >= 0 && page >= pv_base; }
                int gy = sup == 1 ? y - 2 : sup == 2 ? y + 3 : y;
                if (c != 0x20) {
                    const uint8_t *g = prn_font[c - 0x20];
                    for (int r = 0; r < 11; r++) for (int k = 0; k < 6; k++) if (g[r] & (0x20 >> k))
                        for (int q = 0; q < gw + (bold ? 1 : 0); q++) pv_dot(draw, x + k * wd / 6 + q, gy + r);
                }
                if (uline) for (int q = 0; q < wd; q++) pv_dot(draw, x + q, y + 11);
                x += wd;
            } else if (c >= 0x80) {
                int wd = cw * (dwid ? 2 : 1);
                if (x + wd > PV_TW) { x = 0; PV_ADV(ls); draw = target >= 0 && page >= pv_base; }
                pv_cg_glyph(draw, x, y, c, wd, bold);
                x += wd;
            }
            break;
        }
    }
    if (target >= 0) { if (page >= pv_base && page - pv_base < PV_KEEP + 1) pv_ph[page - pv_base] = pl; pv_head = pv_yb + y; }
    if (target < 0 && pv_total > 1 && !pv_ink) pv_total--;     /* 끝의 빈 쪽은 뺌 */
#undef PV_NEWPAGE
#undef PV_ADV
}

/* 자료의 쪽 수 (팝업을 띄울지 정할 때) */
static int pv_count_pages(const uint8_t *d, uint32_t n)
{
    const uint8_t *od = pv_data; uint32_t ol = pv_len;
    pv_data = d; pv_len = n;
    pv_run(-1);
    pv_data = od; pv_len = ol;
    return pv_total;
}

/* 쪽 수를 세고 (마지막 5쪽만 둠), 이어 붙인 용지 그림을 새로 그림 */
static void pv_render(void)
{
    pv_run(-1);
    pv_base = pv_total > PV_KEEP ? pv_total - PV_KEEP : 0;
    pv_npages = pv_total - pv_base;
    pv_run(0);
    int ht = 0;
    for (int k = 0; k < pv_npages; k++) ht += pv_ph[k];
    if (ht > PV_TALL) ht = PV_TALL;
    if (ht < 1) ht = 1;
    pv_ht = ht;
    memset(pv_rowtear, 0, (size_t)ht);
    int b = 0;
    for (int k = 0; k <= pv_npages; k++) {              /* 쪽 경계 둘레 4줄에 뜯는 줄 */
        for (int r = b - 2; r < b + 2; r++) if (r >= 0 && r < ht) pv_rowtear[r] = 1;
        if (k < pv_npages) b += pv_ph[k];
    }
}

static void pv_open(const uint8_t *d, uint32_t n, const char *name)
{
    if (!P.scratch || P.scratch_size < PV_TBYTES + (1u << 19)) { toast(T(S_PRN_FAIL), 0); return; }
    pv_bits = (uint8_t (*)[PV_W / 8])P.scratch;
    pv_data = d; pv_len = n;
    copy_str(pv_name, sizeof pv_name, name);
    pv_zoom = 0; pv_scx = pv_scy = 0;
    pv_korean = key_layout == KL_KR && !machine_japanese();   /* 4.0.9: 한국 MSX만 자모 묶기 */
    pv_follow = 1;                                      /* 라인 프린터: 가장 나중에 나온 줄(인쇄 머리)이 아래에 보이게 */
    pv_run(-1);
    pv_page = pv_total > 1 ? (pv_total > PV_KEEP ? PV_KEEP : pv_total) - 1 : 0;
    snap_valid = 0;
    open_dialog(16);
}

/* 지금 보는 쪽 (화면 가운데에 걸친 쪽) */
static int pv_view_page(void)
{
    if (pv_dh <= pv_ah || pv_k <= 0) return pv_npages - 1;
    int row = (int)((int64_t)(pv_scy + pv_ah / 2) * 72 / pv_k), b = 0;
    for (int k = 0; k < pv_npages; k++) { b += pv_ph[k]; if (row < b) return k; }
    return pv_npages - 1;
}

/* 1비트 BMP로 지금 쪽 저장 (가로 120 dpi에 맞춰 세로를 5/3배로 늘려 종이 모양 그대로) */
static void pv_save_bmp(void)
{
    int pg = pv_view_page(), ps = 0, rows = pv_ph[pg] > 0 ? pv_ph[pg] : PV_H;
    for (int k = 0; k < pg; k++) ps += pv_ph[k];
    const uint32_t ow = PV_W, oh = (uint32_t)rows * 5 / 3, rb = ((ow / 8) + 3) & ~3u, sb = ow / 8, sz = 62 + rb * oh;
    if (!P.scratch || P.scratch_size < PV_TBYTES + sz || !P.create || !P.write_file) { toast(T(S_PRN_FAIL), 0); return; }
    uint8_t *o = P.scratch + PV_TBYTES;                 /* 쪽 그림 뒤 */
    memset(o, 0, 62);
    o[0] = 'B'; o[1] = 'M';
    o[2] = (uint8_t)sz; o[3] = (uint8_t)(sz >> 8); o[4] = (uint8_t)(sz >> 16); o[5] = (uint8_t)(sz >> 24);
    o[10] = 62; o[14] = 40;
    o[18] = (uint8_t)ow; o[19] = (uint8_t)(ow >> 8); o[22] = (uint8_t)oh; o[23] = (uint8_t)(oh >> 8);
    o[26] = 1; o[28] = 1;
    o[34] = (uint8_t)(rb * oh); o[35] = (uint8_t)((rb * oh) >> 8); o[36] = (uint8_t)((rb * oh) >> 16);
    o[54] = o[55] = o[56] = 0; o[58] = o[59] = o[60] = 0xFF;      /* 0 = 검정(잉크), 1 = 흰 종이 */
    for (uint32_t r = 0; r < oh; r++) {
        uint32_t sy = (uint32_t)ps + (oh - 1 - r) * (uint32_t)rows / oh;
        if (sy >= PV_TALL) sy = PV_TALL - 1;
        const uint8_t *s = pv_bits[sy];
        uint8_t *d = o + 62 + r * rb;
        for (uint32_t k = 0; k < rb; k++) d[k] = k < sb ? (uint8_t)~s[k] : 0xFF;
    }
    char path[160];
    if (next_free_name("Printer", "PRN_", ".bmp", path, sizeof path) || P.create(path, 0) || P.write_file(path, o, sz)) { toast(T(S_PRN_FAIL), 0); return; }
    const char *nm = path;
    for (const char *q = path; *q; q++) if (*q == '\\') nm = q + 1;
    toast(T(S_PV_IMG_SAVED), nm);
}

/* 종이 그리기: 면적 평균으로 줄이거나 늘려 (구멍·잉크·뜯는 줄 포함) 한 줄씩 단색 띠로. 이어 붙인 용지 전체를 위아래로 움직임 */
static void pv_paper(uint32_t ax, uint32_t ay, uint32_t aw, uint32_t ah, uint32_t pxk /* 인치당 화면 점 */)
{
    uint32_t dw = PV_PW * pxk / 120, dh = (uint32_t)pv_ht * pxk / 72;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int sx0v = dw > aw ? pv_scx : -(int)((aw - dw) / 2), sy0v = dh > ah ? pv_scy : -(int)((ah - dh) / 2);   /* 화면 안쪽에서 종이의 왼쪽 위 (음수 = 가운데) */
    if (dw > aw) { int mx = (int)(dw - aw); if (sx0v > mx) sx0v = mx; if (sx0v < 0) sx0v = 0; pv_scx = sx0v; }
    if (dh > ah) { int my = (int)(dh - ah); if (sy0v > my) sy0v = my; if (sy0v < 0) sy0v = 0; pv_scy = sy0v; pv_scmax = my; } else pv_scmax = 0;
    pv_ah = (int)ah; pv_dh = (int)dh; pv_k = (int)pxk;
    /* 종이 밖(바탕) */
    fe_fill(ax, ay, aw, ah, C_FIELD);
    uint32_t vx0 = dw > aw ? 0 : (aw - dw) / 2, vy0 = dh > ah ? 0 : (ah - dh) / 2;
    uint32_t vw = dw > aw ? aw : dw, vh = dh > ah ? ah : dh;
    fe_fill_rgb(ax + vx0 + 3 * S, ay + vy0 + 4 * S, vw, vh, 18, 20, 26);       /* 그림자 */
    for (uint32_t oy = 0; oy < vh; oy++) {
        uint32_t gy = (uint32_t)(dh > ah ? sy0v : 0) + oy;
        uint32_t sy0 = gy * (uint32_t)pv_ht / dh, sy1 = (gy + 1) * (uint32_t)pv_ht / dh;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        uint32_t run_x = 0, run_c = 0xFFFFFFFFu;
        for (uint32_t ox = 0; ox <= vw; ox++) {
            uint32_t colr = 0xFFFFFFFFu;
            if (ox < vw) {
                uint32_t gx = (uint32_t)(dw > aw ? sx0v : 0) + ox;
                uint32_t sx0 = gx * PV_PW / dw, sx1 = (gx + 1) * PV_PW / dw, ink = 0, hole = 0, perf = 0, n = 0;
                if (sx1 <= sx0) sx1 = sx0 + 1;
                for (uint32_t sy = sy0; sy < sy1 && sy < (uint32_t)pv_ht; sy++)
                    for (uint32_t sx = sx0; sx < sx1 && sx < PV_PW; sx++, n++) {
                        if (sx >= PV_STRIP && sx < PV_STRIP + PV_W) {
                            uint32_t px = sx - PV_STRIP;
                            if (pv_bits[sy][px >> 3] & (0x80 >> (px & 7))) ink++;
                        } else {
                            uint32_t ex = sx < PV_STRIP ? sx : PV_PW - 1 - sx;      /* 가장자리에서 거리 */
                            int dx = (int)ex - 45, dy = (int)(sy % 36) - 18;
                            if (dx * dx * 29 + dy * dy * 81 <= 9 * 9 * 29)       /* 타원: 가로 반지름 9점, 세로 약 5.4점 */
                                hole++;
                        }
                        if (((sx == PV_STRIP - 1 || sx == PV_STRIP + PV_W) && (sy & 3) < 2) || (pv_rowtear[sy] && (sx & 3) < 2)) perf++;   /* 세로 접는 줄 + 쪽 사이 뜯는 줄 */
                    }
                if (!n) n = 1;
                uint32_t pa = n - ink - hole - perf, c[3];
                static const uint8_t PAPER[3] = { 248, 247, 240 }, INK[3] = { 26, 26, 32 }, HOLE[3] = { 46, 50, 62 }, PERF[3] = { 196, 194, 186 };
                for (int k = 0; k < 3; k++) c[k] = (PAPER[k] * pa + INK[k] * ink + HOLE[k] * hole + PERF[k] * perf) / n;
                colr = c[0] << 16 | c[1] << 8 | c[2];
            }
            if (colr != run_c) {
                if (run_c != 0xFFFFFFFFu && ox > run_x) fe_fill_rgb(ax + vx0 + run_x, ay + vy0 + oy, ox - run_x, 1, run_c >> 16, (run_c >> 8) & 255, run_c & 255);
                run_c = colr; run_x = ox;
            }
        }
    }
}

static void draw_pv(void)
{
    UiRect o;
    char chip[24], num[12];
    chip[0] = 0;
    pv_render();                                        /* scratch가 다른 일에 쓰였을 수 있어 그릴 때마다 다시 풀어 놓음 */
    int_to_str(num, sizeof num, pv_page + 1); str_append(chip, sizeof chip, num); str_append(chip, sizeof chip, " / ");
    int_to_str(num, sizeof num, pv_npages); str_append(chip, sizeof chip, num); str_append(chip, sizeof chip, T(S_PV_PAGE));
    UiRect b = ui_full_card(T(S_PV_TITLE), pv_name, chip, &o);
    hit_add(hc_none, 0, 0, 0, 0, o.x, o.y, o.w, o.h);
    uint32_t cy = dlg_cmd_y();
    uint32_t ax = b.x + 10 * S, ay = b.y + 6 * S, aw = b.w > 20 * S ? b.w - 20 * S : 1, ah = cy > ay + 12 * S ? cy - ay - 8 * S : 1;
    uint32_t k = ah / 11 < aw * 120 / PV_PW ? ah / 11 : aw * 120 / PV_PW;   /* 맞춤: 11 inch 높이, 폭 전체 */
    if (pv_zoom == 1) k = k * 3 / 2; else if (pv_zoom == 2) k = k * 2;
    if (k < 8) k = 8;
    if (pv_follow) {                                    /* 처음 열 때: 인쇄 머리가 화면 아래쪽에 오게 */
        int hy = (int)((int64_t)pv_head * k / 72) + (int)(28 * S);
        pv_scy = hy - (int)ah;
        pv_follow = 0;
    }
    pv_paper(ax, ay, aw, ah, k);
    pv_page = pv_view_page();
    ui_cmdbar(o.x + S, cy, o.w - 2 * S, T(S_PV_KEYS));
}

static void pv_key(int key)
{
    menu_dirty = 1;
    int k = key & 0x7F, ext = key & 0x80, step = pv_k > 0 ? pv_k * 2 / 3 : 40, pg = pv_ah > 0 ? pv_ah * 9 / 10 : 400;
    if (key == 0x01 || key == 0x1C || key == 0x9C) { dialog_back(); return; }
    if (k == 0x1F && !ext) { pv_save_bmp(); return; }                          /* S */
    if (k == 0x23 && !ext) { pv_hcompose = !pv_hcompose; full_redraw(); return; }   /* H: 모아쓰기 ↔ 풀어쓰기 (한국 MSX) */
    if (k == 0x4E || (k == 0x0D && !ext)) { if (pv_zoom < 2) { pv_zoom++; pv_scx = 0; full_redraw(); } return; }   /* + = */
    if (k == 0x4A || (k == 0x0C && !ext)) { if (pv_zoom > 0) { pv_zoom--; pv_scx = 0; full_redraw(); } return; }   /* - */
    if (key == 0xC8) pv_scy -= step;                                           /* ↑ */
    else if (key == 0xD0) pv_scy += step;                                      /* ↓ */
    else if (key == 0xC9) pv_scy -= pg;                                        /* PgUp */
    else if (key == 0xD1) pv_scy += pg;                                        /* PgDn */
    else if (key == 0xC7) pv_scy = 0;                                          /* Home */
    else if (key == 0xCF) pv_scy = 1 << 28;                                    /* End */
    else if (key == 0xCB) pv_scx -= step;
    else if (key == 0xCD) pv_scx += step;
    if (pv_scy < 0) pv_scy = 0;
    if (pv_scy > pv_scmax) pv_scy = pv_scmax;
}
