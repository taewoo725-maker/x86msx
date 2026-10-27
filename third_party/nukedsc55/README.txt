Nuked-SC55 (Roland SC-55 계열 실기 롬 에뮬레이션)
  원본: https://github.com/nukeykt/Nuked-SC55 (nukeykt, 2026-09-12 판 cc801b2)
  라이선스: GPL-2.0-or-later (LICENSE). 2026 년에 원 저자가 GPL v2 이상으로 다시 라이선스함.
  X86MSX 와 함께 GPL-3.0-or-later 로 배포.

X86MSX 에서 바꾼 것 (5.2.60):
  - .cpp → .c (C 로 빌드: struct typedef, 기본 인자, inline → static inline, 상수 → #define)
  - SDL·작업 스레드·main·오디오 장치·화면 창 뺌 (mcu.c, lcd.c). MCU_Step = 원본 work_thread 한 바퀴
  - JV-880 웨이브 롬(카드·확장) 뺌 — SC-55 / SC-55mkII 만
  - nsc_shim.h: printf 없앰, 전역 이름에 nsc_ 붙임 (X86MSX 의 ram·timer 등과 안 겹치게)
  - sc55w.c/h: X86MSX 감싸개 (롬 넣기, MIDI, 48 kHz 표본, LCD 글자·막대)
  - 레거시(BIOS 32비트) 판에는 넣지 않음 (src/sc55_all.c 의 빈 함수) — 메모리 배치 자리 부족

롬은 들어 있지 않음. 사용자가 USB\SC55\ 에 넣음:
  SC-55mkII: rom1.bin rom2.bin waverom1.bin waverom2.bin rom_sm.bin
  SC-55    : sc55_rom1.bin sc55_rom2.bin sc55_waverom1.bin sc55_waverom2.bin sc55_waverom3.bin
