/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* mp4skiptest (8.0.139) - 비참조 프레임 해독 생략 규칙 시험 */
#include <stdio.h>
#include "../src/mp4skip.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("실패 줄 %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void)
{
    /* NAL 첫 바이트: forbidden(1) ref_idc(2) type(5) */
    CHECK(mp4skip_nal_type(0x41) == 1 && mp4skip_nal_ref_idc(0x41) == 2);   /* P 조각 (ref_idc 2) */
    CHECK(mp4skip_nal_type(0x01) == 1 && mp4skip_nal_ref_idc(0x01) == 0);   /* 비참조 B 조각 */
    CHECK(mp4skip_nal_type(0x65) == 5 && mp4skip_nal_ref_idc(0x65) == 3);   /* IDR */
    /* 켜졌고 뒤에 해독할 것이 있고 비참조 일반 조각일 때만 건너뜀 */
    CHECK(mp4skip_can_skip(1, 0x01, 1) == 1);
    CHECK(mp4skip_can_skip(0, 0x01, 1) == 0);      /* 스위치 꺼짐 */
    CHECK(mp4skip_can_skip(1, 0x01, 0) == 0);      /* 이번 호출의 마지막 샘플 → 화면에 나올 수 있음 */
    CHECK(mp4skip_can_skip(1, 0x41, 1) == 0);      /* 참조되는 P */
    CHECK(mp4skip_can_skip(1, 0x21, 1) == 0);      /* ref_idc 1 */
    CHECK(mp4skip_can_skip(1, 0x65, 1) == 0);      /* IDR */
    CHECK(mp4skip_can_skip(1, 0x06, 1) == 0);      /* SEI 등 다른 종류 */
    CHECK(mp4skip_can_skip(1, 0x09, 1) == 0);
    if (fails) { printf("mp4skiptest 실패 %d\n", fails); return 1; }
    printf("mp4skiptest 통과\n");
    return 0;
}
