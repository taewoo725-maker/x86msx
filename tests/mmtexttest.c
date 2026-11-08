/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86 Multimedia 글자 그리기 (30h TEXT, 8.0.88) 시험: 영상 포트 24h~27h 로 그대로 부려서 확인 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/mmvid.h"
#include "../src/mmtext.h"

extern UiFont font_t9;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)
#define ST_ERR 8

typedef struct { int ok, used, drawn, w, h; uint8_t bm[1100]; int nbm; uint8_t st; } Res;
static Res text(int code, int h, int maxw, const void *s, int n)
{
    Res r; memset(&r, 0, sizeof r);
    mmvid_write(0, 0x30);
    uint8_t a[5] = { (uint8_t)code, (uint8_t)h, (uint8_t)maxw, (uint8_t)(maxw >> 8), (uint8_t)n };
    for (int i = 0; i < 5; i++) mmvid_write(2, a[i]);
    const uint8_t *p = s; for (int i = 0; i < n; i++) mmvid_write(2, p[i]);
    r.st = mmvid_read(0);
    if (r.st & ST_ERR) return r;
    r.ok = 1; r.used = mmvid_read(1); r.drawn = mmvid_read(1); r.w = mmvid_read(1); r.w |= mmvid_read(1) << 8; r.h = mmvid_read(1);
    r.nbm = ((r.w + 7) / 8) * r.h; for (int i = 0; i < r.nbm; i++) r.bm[i] = mmvid_read(1);
    return r;
}
static int same(const Res *a, const Res *b) { return a->ok && b->ok && a->w == b->w && a->h == b->h && a->nbm == b->nbm && !memcmp(a->bm, b->bm, a->nbm); }
static void art(const Res *r)
{
    int bpr = (r->w + 7) / 8;
    for (int y = 0; y < r->h; y++) { for (int x = 0; x < r->w; x++) putchar((r->bm[y * bpr + x / 8] >> (7 - x % 8)) & 1 ? '#' : '.'); putchar('\n'); }
}
static int has_glyph(uint32_t cp)
{
    int lo = 0, hi = font_t9.count - 1;
    while (lo <= hi) { int m = (lo + hi) / 2; uint32_t c = font_t9.glyphs[m].cp; if (c == cp) return 1; if (c < cp) lo = m + 1; else hi = m - 1; }
    return 0;
}
static int ink_rows(const Res *r, int *first, int *last)
{
    int bpr = (r->w + 7) / 8, n = 0; *first = -1; *last = -1;
    for (int y = 0; y < r->h; y++) { int any = 0; for (int i = 0; i < bpr; i++) if (r->bm[y * bpr + i]) any = 1; if (any) { n++; if (*first < 0) *first = y; *last = y; } }
    return n;
}

int main(void)
{
    mmvid_reset();
    /* IDENT */
    mmvid_write(0, 0x01); uint8_t id[5]; for (int i = 0; i < 5; i++) id[i] = mmvid_read(1);
    CHECK(id[0] == 'X' && id[1] == 'V' && id[2] == 'D' && id[3] == 1, "IDENT 글자");
    CHECK(id[4] & 4, "IDENT 5번째 bit2 (글자 그리기) = %02X", id[4]);

    /* 기본: 한글 + 영문 */
    const char *u8 = "\xED\x95\x9C\xEA\xB8\x80 test";               /* 한글 test */
    Res a = text(0, 9, 0, u8, (int)strlen(u8));
    CHECK(a.ok && a.used == 11 && a.drawn == 7, "UTF-8 한글 test: ok=%d used=%d drawn=%d", a.ok, a.used, a.drawn);
    CHECK(a.h == 10 && a.w > 20 && a.w < 80, "폭 %d 높이 %d", a.w, a.h);
    int f, l; int rows = ink_rows(&a, &f, &l);
    CHECK(rows >= 9 && f == 0, "획이 있는 줄 %d (처음 %d, 끝 %d)", rows, f, l);
    art(&a);
    /* 한글 한 글자는 맨 아래(영문 꼬리) 줄이 비어야 함 */
    Res g1 = text(0, 9, 0, "\xEA\xB0\x80", 3);                      /* 가 */
    ink_rows(&g1, &f, &l);
    CHECK(g1.w == 10 && g1.drawn == 1 && l <= 8, "가: 폭 %d 끝줄 %d", g1.w, l);

    /* 부호마다 같은 그림 */
    Res c949 = text(1, 9, 0, "\xC7\xD1\xB1\xDB test", 9);          /* CP949 한글 test */
    uint8_t u16le[] = { 0x5C, 0xD5, 0x00, 0xAE, 0x20, 0x00, 't', 0, 'e', 0, 's', 0, 't', 0 };
    uint8_t u16be[] = { 0xD5, 0x5C, 0xAE, 0x00, 0x00, 0x20, 0, 't', 0, 'e', 0, 's', 0, 't' };
    Res le = text(2, 9, 0, u16le, sizeof u16le), be = text(3, 9, 0, u16be, sizeof u16be);
    CHECK(same(&a, &c949) && a.used == 11 && c949.used == 9, "CP949 가 UTF-8 과 다름 (used %d)", c949.used);
    CHECK(same(&a, &le) && le.used == 14, "UTF-16LE 가 UTF-8 과 다름");
    CHECK(same(&a, &be) && be.used == 14, "UTF-16BE 가 UTF-8 과 다름");
    uint8_t bom[] = { 0xFF, 0xFE, 0x5C, 0xD5, 0x00, 0xAE, 0x20, 0x00, 't', 0, 'e', 0, 's', 0, 't', 0 };
    Res bm = text(2, 9, 0, bom, sizeof bom);
    CHECK(same(&a, &bm) && bm.used == 16 && bm.drawn == 7, "BOM 은 건너뜀 (drawn %d)", bm.drawn);

    /* 폭 제한: 글자 단위로 자름 */
    Res cut = text(0, 9, 25, "\xED\x95\x9C\xEA\xB8\x80\xEA\xB0\x80", 9);   /* 한글가 → 25 픽셀 = 2 글자 */
    CHECK(cut.ok && cut.drawn == 2 && cut.used == 6 && cut.w == 20, "폭 25 → drawn=%d used=%d w=%d", cut.drawn, cut.used, cut.w);
    Res none = text(0, 9, 5, "\xED\x95\x9C", 3);
    CHECK(none.ok && none.drawn == 0 && none.used == 0 && none.w == 0, "폭 5 에 한글 못 들어감");

    /* 없는 글자·깨진 바이트 = ? 한 글자 */
    Res q = text(0, 9, 0, "?", 1);
    Res bad = text(0, 9, 0, "\xFF", 1), emo = text(0, 9, 0, "\xF0\x9F\x98\x80", 4), trunc = text(0, 9, 0, "\xED\x95", 2);
    CHECK(same(&q, &bad) && bad.drawn == 1, "깨진 바이트가 ? 가 아님");
    CHECK(same(&q, &emo) && emo.used == 4 && emo.drawn == 1, "BMP 밖 글자가 ? 가 아님 (used %d)", emo.used);
    CHECK(trunc.drawn >= 1 && trunc.used == 2, "잘린 UTF-8: drawn=%d used=%d", trunc.drawn, trunc.used);
    Res bad949 = text(1, 9, 0, "\xA1", 1);
    CHECK(same(&q, &bad949), "CP949 외톨이 앞바이트가 ? 가 아님");

    /* 오류: 높이·부호·길이 */
    int h0 = text(0, 0, 0, "a", 1).ok, h40 = text(0, 40, 0, "a", 1).ok, h7 = text(0, 7, 0, "a", 1).ok, c9 = text(9, 9, 0, "a", 1).ok;
    CHECK(!h0 && !h40 && !h7 && !c9, "범위 밖 값이 받아들여짐 (%d %d %d %d)", h0, h40, h7, c9);
    CHECK(text(0, 8, 0, "a", 1).ok && text(0, 16, 0, "a", 1).ok, "경계 8·16 이 거부됨");
    { uint8_t big[201]; memset(big, 'a', sizeof big); Res r = text(0, 9, 0, big, 201); CHECK(!r.ok && (r.st & ST_ERR), "길이 201 이 받아들여짐"); }
    { uint8_t big[200]; memset(big, 'W', sizeof big); Res r = text(0, 9, 0, big, 200); CHECK(r.ok && r.w <= 512, "길이 200 폭 %d", r.w); }
    Res zero = text(0, 9, 0, "", 0);
    CHECK(zero.ok && zero.w == 0 && zero.drawn == 0, "빈 문자열");

    /* 오류 뒤 정상 */
    CHECK(text(0, 9, 0, "a", 1).ok, "오류 뒤 정상 호출이 안 됨");

    /* 영상 쪽 상태를 건드리지 않음: OPEN 실패(오류 비트) → TEXT → 다른 명령 뒤 상태 */
    mmvid_write(0, 0x11); for (int i = 0; i < 12; i++) mmvid_write(2, 0);
    uint8_t s0 = mmvid_read(0);
    (void)text(0, 9, 0, "ab", 2);
    mmvid_write(0, 0x09);                                           /* POS: 인자 없는 명령 → 영상 쪽 상태 */
    uint8_t s1 = mmvid_read(0);
    CHECK((s0 & ST_ERR) == (s1 & ST_ERR), "TEXT 가 영상 오류 비트를 바꿈 (%02X → %02X)", s0, s1);

    /* 한글 11,172자가 모두 글꼴에 있음, 자모·가나·한자도 */
    int miss = 0; for (uint32_t cp = 0xAC00; cp <= 0xD7A3; cp++) if (!has_glyph(cp)) miss++;
    CHECK(miss == 0, "한글 음절 %d자 없음", miss);
    int jm = 0; for (uint32_t cp = 0x3131; cp <= 0x318E; cp++) if (!has_glyph(cp)) jm++;
    int kana = 0; for (uint32_t cp = 0x3041; cp <= 0x3093; cp++) if (!has_glyph(cp)) kana++;
    int hanja = 0; for (uint32_t cp = 0x4E00; cp <= 0x4E10; cp++) if (!has_glyph(cp)) hanja++;
    printf("글꼴: 글자 %d개, 자모 빠짐 %d, 히라가나 빠짐 %d, 한자(4E00~4E10) 빠짐 %d\n", font_t9.count, jm, kana, hanja);
    CHECK(jm <= 4 && kana == 0, "자모/가나 빠짐");

    /* 한글 음절 전부를 UTF-16LE 로 50자씩 그려 (폭 500 ≤ 512) ? 로 바뀐 것이 없고 폭이 10 × 50 */
    { uint8_t buf[200]; int bad_n = 0;
      for (uint32_t cp = 0xAC00; cp <= 0xD7A3; cp += 50) {
          int n = 0; for (uint32_t c = cp; c < cp + 50 && c <= 0xD7A3; c++) { buf[n++] = (uint8_t)c; buf[n++] = (uint8_t)(c >> 8); }
          Res r = text(2, 9, 0, buf, n);
          if (!r.ok || r.drawn != n / 2 || r.w != 10 * (n / 2)) bad_n++;
      }
      CHECK(bad_n == 0, "한글 전체 그리기에서 어긋난 묶음 %d", bad_n); }

    { uint8_t buf[200]; int n = 0; for (int i = 0; i < 100; i++) { buf[n++] = 0x00; buf[n++] = 0xAC; }
      Res r = text(2, 9, 0, buf, n); CHECK(r.ok && r.drawn == 51 && r.w == 510 && r.used == 102, "폭 512 상한: drawn=%d w=%d used=%d", r.drawn, r.w, r.used); }

    /* CP949: 표의 모든 두 바이트 글자가 한 글자로 그려짐 (? 포함 아님: 표가 있는 칸만) */
    { extern const uint16_t cp949_tab[126 * 190]; int bad_n = 0, tot = 0;
      for (int le2 = 0; le2 < 126; le2++) for (int tr = 0; tr < 190; tr++) {
          uint16_t u = cp949_tab[le2 * 190 + tr]; if (!u) continue; tot++;
          if (le2 % 5 || tr % 7) continue;                                     /* 표본 */
          uint8_t b[2] = { (uint8_t)(0x81 + le2), (uint8_t)(0x41 + tr) };
          Res r = text(1, 9, 0, b, 2);
          if (!r.ok || r.used != 2 || r.drawn != 1) bad_n++;
      }
      CHECK(bad_n == 0 && tot > 8000, "CP949 표본 어긋남 %d (표 %d칸)", bad_n, tot); }

    /* 높이 11~16 은 내장 큰 글꼴(없으면 같은 글꼴)로 그려도 형식은 같음 */
    Res h12 = text(0, 12, 0, u8, (int)strlen(u8));
    CHECK(h12.ok && h12.h >= 10 && h12.drawn == 7, "높이 12: h=%d drawn=%d", h12.h, h12.drawn);

    printf("mmtexttest: %s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
