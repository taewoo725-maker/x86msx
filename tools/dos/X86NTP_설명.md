# X86NTP 1.0 — MSX-DOS에서 인터넷 시간으로 시계 맞추기

MSX-DOS 1/2, Nextor 에서 `X86NTP` 를 실행하면 인터넷 시간 서버(SNTP)에서 지금 시각을 받아
MSX 시계를 맞추고, X86MSX 가 그 값을 **PC 시계에도** 반영합니다.

## 필요한 것
- 네트워크 카트리지(TCP/IP UNAPI)가 꽂혀 있고 유선랜 또는 무선랜이 연결된 상태
- X86MSX 8.0.43 이상 (PC 시계 반영은 8.0.43부터. 그 전 판에서는 MSX 시계만 맞음)

## 쓰는 법
```
X86NTP                  기본: kr.pool.ntp.org → time.google.com → pool.ntp.org 순서로 시도, 한국 시간(+9)
X86NTP time.google.com  서버 지정
X86NTP /Z-5             시간대 지정 (예: /Z-5, /Z+5:30. 기본 +9, ±14시간까지)
X86NTP /T               시험: 시간만 보여 주고 시계는 안 바꿈
X86NTP /?               사용법
```
화면 예:
```
X86NTP 1.0 - Internet time (SNTP) for X86MSX
Server: kr.pool.ntp.org
Time:   2026-10-31 21:34:56 (UTC+9)
Clock set.
```

## PC 시계 반영 (X86MSX 쪽)
- MSX 에서 시계를 고치면(X86NTP, SET TIME, SET DATE 등) 2초 정도 잠잠해진 뒤 PC 시계를 같은 값으로 고칩니다.
- 끄려면 `x86msx.ini` 에 `PcClockSet=0` (기본 1). 끄면 MSX 시계만 바뀌고 PC 시계는 그대로입니다.
- PC 시계를 못 고치는 PC 는 그냥 MSX 시계만 바뀌고 넘어갑니다.

## 한계
- 시각은 초 단위, 네트워크 지연 보정은 하지 않습니다(보통 1초 안쪽).
- NTP 32비트 시각이라 2036년 이후는 맞지 않습니다.
- 서머타임 자동 처리는 없습니다. `/Z` 로 직접 지정하세요.

## 만든 방법
`pasmo x86ntp.asm X86NTP.COM` (소스는 `x86ntp.asm`). 시험은 `tests/ntptest.sh` —
진짜 UNAPI 롬과 네트워크 카드, 가짜 시간 서버로 날짜 계산(윤년), 시간대, 오류 처리를 확인합니다.
