/* X86MSX 5.0.180 - Roland MT-32 / CM-32L 음원 (munt mt32emu 2.7.x, LGPL-2.1+) 을 본체에 붙이는 C 연결부.
 *   롬은 사용자가 USB 의 MT32 폴더에 넣은 것을 쓴다 (배포하지 않음). 48 kHz 정확 모드로 만들어 변환기 없이 그대로 씀.
 *   메모리는 부르는 쪽이 준 한 덩어리(arena)에서만 가져오고, 닫으면 통째로 되돌린다. 예외·RTTI·libm 없음. */
#include <stddef.h>
#include <stdint.h>
#include "mt32emu.h"
#include "mt32w.h"

/* 5.2.173: 엔진 슬롯마다 자기 메모리 덩어리 (new 는 "지금 슬롯" 덩어리에서 가져옴 — 부르는 함수마다 먼저 슬롯을 고름) */
struct Eng;
static struct Eng *E;
static uint8_t *g_arena; static size_t g_size, g_used;   /* 지금 슬롯의 값 (sel 이 채움) */
static size_t g_peak;

void *operator new(size_t n) { n = (n + 15) & ~(size_t)15; if (!g_arena || g_used + n > g_size) { return 0; } void *p = g_arena + g_used; g_used += n; if (g_used > g_peak) g_peak = g_used; return p; }
void *operator new[](size_t n) { return operator new(n); }
void operator delete(void *) {}
void operator delete[](void *) {}
void operator delete(void *, size_t) {}
void operator delete[](void *, size_t) {}
#ifdef FE_UEFI
extern "C" int _purecall(void) { for (;;) {} }
extern "C" int atexit(void (*)(void)) { return 0; }   /* 지역 정적 객체의 소멸자 등록 (프로그램은 끝나지 않으므로 필요 없음) */
extern "C" int abs(int x) { return x < 0 ? -x : x; }
extern "C" int rand(void) { static uint32_t s = 12345; s = s * 1103515245u + 12345u; return (int)((s >> 16) & 0x7FFF); }
extern "C" double fmod(double a, double) { return a; }       /* 실수 방식 렌더러에서만 쓰임 (안 씀) */
extern "C" char *strcpy(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) {} return r; }
extern "C" char *strncpy(char *d, const char *s, size_t n) { size_t i = 0; for (; i < n && s[i]; i++) d[i] = s[i]; for (; i < n; i++) d[i] = 0; return d; }
/* 아주 작은 vsnprintf: %d %u %x %X %c %s (폭·0 채움 지원), 디버그 문구용 */
extern "C" int mt_vsnprintf(char *b, unsigned n, const char *f, va_list ap) {
	unsigned o = 0;
	#define PUT(c) do { if (o + 1 < n) b[o] = (char)(c); o++; } while (0)
	for (; *f; f++) {
		if (*f != '%') { PUT(*f); continue; }
		f++; int zero = 0, w = 0, lng = 0;
		if (*f == '0') { zero = 1; f++; }
		while (*f >= '0' && *f <= '9') w = w * 10 + (*f++ - '0');
		while (*f == 'l' || *f == 'z') { lng = 1; f++; }
		char t[24]; int k = 0; unsigned long long v;
		switch (*f) {
		case 'd': { long long x = lng ? va_arg(ap, long) : va_arg(ap, int); int neg = x < 0; v = neg ? (unsigned long long)-x : (unsigned long long)x; do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v); if (neg) t[k++] = '-'; break; }
		case 'u': v = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned); do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v); break;
		case 'x': case 'X': v = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned); do { int d = (int)(v & 15); t[k++] = (char)(d < 10 ? '0' + d : (*f == 'x' ? 'a' : 'A') + d - 10); v >>= 4; } while (v); break;
		case 'c': t[k++] = (char)va_arg(ap, int); break;
		case 's': { const char *s = va_arg(ap, const char *); if (!s) s = "(null)"; while (*s) { PUT(*s); s++; } continue; }
		case '%': PUT('%'); continue;
		default: continue;
		}
		while (k < w) { t[k++] = zero ? '0' : ' '; }
		while (k) PUT(t[--k]);
	}
	if (n) b[o < n ? o : n - 1] = 0;
	return (int)o;
	#undef PUT
}
extern "C" int vsnprintf(char *b, unsigned n, const char *f, va_list ap) { return mt_vsnprintf(b, n, f, ap); }
extern "C" int snprintf(char *b, unsigned n, const char *f, ...) { va_list ap; va_start(ap, f); int r = mt_vsnprintf(b, n, f, ap); va_end(ap); return r; }
extern "C" int sprintf(char *b, const char *f, ...) { va_list ap; va_start(ap, f); int r = mt_vsnprintf(b, 4096, f, ap); va_end(ap); return r; }
extern "C" int vprintf(const char *, va_list) { return 0; }
#endif

using namespace MT32Emu;

struct Eng { uint8_t *arena; size_t size, used, peak; Synth *syn; DefaultMidiStreamParser *par; ArrayFile *cf, *pf; const ROMImage *ci, *pi; char name[40]; };
static Eng eng[MT32W_N];
static void sel(int slot) { if (E) { E->used = g_used; E->peak = g_peak; } E = &eng[slot < 0 || slot >= MT32W_N ? 0 : slot]; g_arena = E->arena; g_size = E->size; g_used = E->used; g_peak = E->peak; }
static void sync(void) { E->used = g_used; E->peak = g_peak; }
#define g_syn (E->syn)
#define g_par (E->par)
#define g_cf (E->cf)
#define g_pf (E->pf)
#define g_ci (E->ci)
#define g_pi (E->pi)
#define g_name (E->name)

extern "C" {

void mt32w_arena(int slot, void *mem, uint32_t size) { sel(slot); E->arena = (uint8_t *)mem; E->size = size; g_arena = E->arena; g_size = size; g_used = 0; g_peak = 0; g_syn = 0; g_par = 0; g_cf = g_pf = 0; g_ci = g_pi = 0; sync(); }
uint32_t mt32w_peak(int slot) { sel(slot); return (uint32_t)g_peak; }

int mt32w_open(int slot, const uint8_t *ctl, uint32_t ctl_n, const uint8_t *pcm, uint32_t pcm_n) {
	sel(slot);
	g_used = 0; g_syn = 0; g_par = 0;                           /* 이전 것은 버림 (통째로 되돌림) */
	int rc = 0;
	do {
	g_cf = new ArrayFile(ctl, ctl_n); g_pf = new ArrayFile(pcm, pcm_n);
	if (!g_cf || !g_pf) { rc = -4; break; }
	g_ci = ROMImage::makeROMImage(g_cf); g_pi = ROMImage::makeROMImage(g_pf);
	if (!g_ci || !g_pi) { rc = -4; break; }
	const ROMInfo *ci = g_ci->getROMInfo(), *pi = g_pi->getROMInfo();
	if (!ci || !pi || ci->type != ROMInfo::Control || pi->type != ROMInfo::PCM) { rc = -1; break; }     /* 알 수 없는 롬 (파일이 맞지 않음) */
	g_syn = new Synth();
	if (!g_syn) { rc = -4; break; }
	if (!g_syn->open(*g_ci, *g_pi, DEFAULT_MAX_PARTIALS, AnalogOutputMode_ACCURATE)) { g_syn = 0; rc = -2; break; }
	g_par = new DefaultMidiStreamParser(*g_syn);
	if (!g_par) { g_syn = 0; rc = -4; break; }
	const char *d = ci->shortName; int k = 0;
	while (*d && k < 38) g_name[k++] = *d++;
	g_name[k] = 0;
	} while (0);
	sync();
	return rc;
}
void mt32w_close(int slot) { sel(slot); if (g_syn) g_syn->close(); g_syn = 0; g_par = 0; g_used = 0; sync(); }
int mt32w_ready(int slot) { sel(slot); return g_syn != 0; }
const char *mt32w_name(int slot) { sel(slot); return g_name; }
void mt32w_midi(int slot, uint8_t b) { sel(slot); if (g_par) g_par->parseStream(&b, 1); sync(); }
void mt32w_reset(int slot) {
	static const uint8_t rst[] = { 0xF0, 0x41, 0x10, 0x16, 0x12, 0x7F, 0x00, 0x00, 0x00, 0x01, 0xF7 };   /* 모든 값 초기화 (모든 음 끔) */
	sel(slot); if (g_par) g_par->parseStream(rst, sizeof rst); sync();
}
void mt32w_fx(int pct) { for (int q = 0; q < MT32W_N; q++) { sel(q); if (g_syn) g_syn->setReverbOutputGain((float)pct / 100.0f); sync(); } }
void mt32w_render(int slot, int32_t *l, int32_t *r, int n) {
	int16_t buf[2 * 256];
	int at = 0;
	sel(slot);
	while (at < n) {
		int c = n - at > 256 ? 256 : n - at;
		if (g_syn) g_syn->render(buf, (Bit32u)c); else for (int i = 0; i < 2 * c; i++) buf[i] = 0;
		for (int i = 0; i < c; i++) { l[at + i] = buf[2 * i]; r[at + i] = buf[2 * i + 1]; }
		at += c;
	}
	sync();
}

} // extern "C"
