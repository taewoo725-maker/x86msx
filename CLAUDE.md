# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 프로젝트 개요

X86MSX: 운영체제 없이 UEFI(x86-64)로 직접 부팅하는 MSX 에뮬레이터 (MSX1/2/2+/turbo R, V9990 등). C(C11)와 일부 NASM 어셈블리, 일부 C++(MT-32, OpenH264, ALAC)로 작성. GPLv3+. 소스 주석·README·커밋 메시지는 **한국어**이고, 파일 머리에 `만든이: KimTaeWoo / 도와준이: Lee.J.H` 와 SPDX 헤더를 둡니다 (새 파일도 맞출 것). 현재 버전은 `kernel/version.h`의 `MSXEMU_VERSION` (8.0.x, "다중CPU판").

## 빌드·시험 명령

모든 것은 루트 `Makefile` 하나로 돌립니다. 산출물은 `build/`.

```bash
make test                       # 호스트(gcc)에서 단위·장치 시험 전체 (build/*test 를 빌드하고 실행)
make regress                    # 기종 부팅 회귀 시험 (python3 tests/regress.py)
make check                      # test + regress (판올림 전 확인)
make regress-update             # 일부러 동작을 바꿨을 때만: 회귀 기준 해시 갱신
make zex                        # Z80 ZEXDOC/ZEXALL (각 1분 내외)
make build/BOOTX64.EFI          # UEFI 실행 파일 (clang + lld-link, nasm 필요)
make image                      # build/msx1.img 부팅 이미지 (tools/mkimage.py)
make clean
```

- **단일 시험 실행**: `make build/psgtest && ./build/psgtest` 처럼 시험 하나만 빌드·실행. `make test`의 실행 단계는 Makefile 270행 근처 `test:` 규칙에 나열돼 있으므로, 새 시험을 추가하면 `test:` 의존 목록과 실행 줄에 함께 넣어야 합니다.
- **회귀 시험 일부만**: `python3 tests/regress.py 이름...` (목록은 `tests/regress/cases.txt`; 기종 ROM이 없으면 SKIP). `--twice`는 결정성 확인. 화면(PPM)·소리(WAV)의 SHA-1을 `tests/regress/expected.txt`와 비교합니다. 상용 ROM은 `tests/regress/local/`(gitignore)에만 둡니다.
- **헤드리스 실행**: `./build/msxrun <기종 폴더> <프레임수> [--shot 프레임 파일.ppm] [--type 프레임 문자열] [--text]` (예: `machines/C-BIOS`).
- **프런트엔드(메뉴 UI) 시험**: `build/fehost` 는 `kernel/fe.c` 를 리눅스에서 돌립니다. 예: `ROOT=/tmp/usb W=1280 H=800 KEYS="f10,down,enter" GAP=20 SHOT=out ./build/fehost`. UI 변경은 이걸로 PPM을 찍어 확인합니다. `fehost_asan`은 ASan판.
- 호스트 시험은 `gcc`, UEFI 빌드는 `clang`, `lld-link`(lld), `nasm`, `python3`가 필요합니다.

## 아키텍처

같은 에뮬레이터 코어(`src/`)를 **두 가지 호스트**가 감싸는 구조입니다. 코어는 플랫폼에 의존하지 않으므로 한쪽에서만 도는 코드를 코어에 넣지 마세요.

- `src/` — 에뮬 코어: Z80/R800(`z80.c`), VDP(`vdp.c`, `v9990.c`), 소리 칩(psg/opll/y8950/scc/opl4/gmsynth/sc55), 카트리지·매퍼(`cart.c`, `mappers/*.inc`), 디스크/IDE/SCSI(fdc, ide, wd33c93, mb89352), 네트워크 카드, X86 Multimedia 카트리지(`mm*.c`, `mp4*.c`, `avi.c`), 상태 저장(`msxstate.c`). 중심 구조체는 `MSX`(`src/msx.h`, 구현 `msx.c`).
- `host/` — 리눅스용 얇은 호스트. `msx_host.c`(프로필 폴더에서 `MSX` 구성)와 `headless.c`(`msxrun`). 단위·회귀 시험은 전부 이 경로로 코어를 구동합니다.
- `kernel/` — UEFI 위에서 도는 "본체": 프런트엔드 `fe.c`(약 1만 줄)와 그 조각 `fe_*.inc`(`fe.c`가 `#include`함, 별도 컴파일 단위 아님), 메뉴·시스템 보기·파일 관리자, 기종 편집기(`machine.c`, `medit.c`), FAT/VFAT/가상 하드(`fat.c`, `vfat.c`, `hdfat.c`), 압축 해제(zipx/arcx/lhasa), 자체 libc/libm(`libc.c`, `libm.c`, `kernel/include`). 기종 폴더는 blueMSX의 `config.ini` + `Machines` 구조를 그대로 읽습니다 (`machines/`).
- `uefi/` — 플랫폼 드라이버: `main.c`(진입점 `efi_main`), USB 키보드·마우스·패드·MIDI, 유선/무선 NIC(`wpa.c`, rtw*, `usbnic_*`), HDMI/디스플레이·HD Audio 진단, 다중 코어 깨우기(`mpwake.c` + `mpwake_tramp.asm` → 생성물 `mpwake_tramp.inc`).
- `third_party/` — 포함한 외부 구성요소(lwIP, mt32emu, openh264, nukedsc55 등). 라이선스는 `THIRD-PARTY-NOTICES.txt`.

핵심 사항:
- **UEFI 빌드는 freestanding**: `-ffreestanding -nostdinc`, 표준 라이브러리 없음. `kernel/libc.c`, `libm.c`, `kernel/include`의 자체 헤더를 씁니다. 호스트 빌드는 진짜 libc를 쓰므로, 코어/커널 코드는 두 환경 모두에서 컴파일돼야 하고 UEFI 쪽 전용 코드는 `FE_UEFI` 매크로로 가릅니다.
- **소스 목록이 두 벌**: 새 `.c` 파일을 추가하면 호스트용 `CORE`(또는 `FEHOST`)와 UEFI용 `USRC` 두 목록에 모두 넣어야 합니다 (Makefile 상단 / 390행 근처). `.inc` 의존성은 Makefile 규칙의 `src/*.inc kernel/*.inc` 와일드카드로 잡힙니다.
- **다중 코어(8.0)**: 본체가 코어 1, 나머지는 일꾼 코어 풀(화면 확대 줄 그리기, MIDI 합성, 영상 녹화 압축). 결과가 단일 코어와 **바이트 단위로 같아야** 합니다 (`tests/vrectest.sh` 등).
- **Z80 코어 동등성**: `build/corefuzz`가 `src/z80.c` 를 동결된 기준 코어 `tests/ref/z80_ref.c` 와 무작위 비교합니다. Z80 코어를 고치면 이 시험이 기준입니다.
- **회귀 해시가 계약**: 화면·소리 해시가 달라지면 의도한 변경인지 먼저 확인하고, 아니면 고쳐야 합니다. 프로젝트 원칙은 "실제 PC에서 잘 되던 동작은 바꾸지 않는다"입니다 (실제 노트북·데스크톱에서 직접 부팅해 검증).
- 판올림 시 `kernel/version.h` 갱신, README(`README.md`, `README_EN.md`)의 버전 배지도 같이 갱신하는 관례입니다.
- 상용 BIOS/게임 ROM은 저장소에 없습니다. 기본 포함은 C-BIOS(`machines/C-BIOS`, `tests/machines/cbios*`)뿐입니다.
