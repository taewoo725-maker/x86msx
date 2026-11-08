/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* sndlattest (8.0.138) - 소리 목표 지연 규칙 시험 */
#include <stdio.h>
#include "../kernel/sndlat.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("실패 줄 %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void)
{
    const int ring = 4096, base = 1600, over_limit = ring - SNDLAT_OVER_MARGIN;
    /* 새 방식: 상한 = 3840-800-480 = 2560, 한 프레임 쓴 뒤에도 10 ms 여유 */
    int cap = sndlat_max_target(ring);
    CHECK(cap == 2560);
    CHECK(cap + SNDLAT_FRAME_NOMINAL + SNDLAT_SAFETY <= over_limit);
    int t = base; for (int i = 0; i < 10; i++) { t = sndlat_after_underrun(t, ring); CHECK(t <= cap); }
    CHECK(t == cap);
    CHECK(sndlat_after_underrun(2800, ring) == cap);          /* 상한 밖 값은 상한으로 */
    CHECK(sndlat_after_overflow(2560, base) == 2160);
    CHECK(sndlat_after_overflow(1700, base) == base);         /* base 아래로 안 내려감 */
    CHECK(sndlat_after_overflow(base, base) == base);
    /* 작은 링에서도 상한이 음수가 되지 않는 값은 아님 → 부르는 쪽은 4096 고정 */
    if (fails) { printf("sndlattest 실패 %d\n", fails); return 1; }
    printf("sndlattest 통과\n");
    return 0;
}
