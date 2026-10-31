# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# MSX1 에뮬레이터 - 호스트 빌드 (설계서 §11 2~6단계)
CC      ?= gcc
CFLAGS  ?= -O2 -std=c11 -Wall -Wextra
CORE    = src/z80.c src/vdp.c src/psg.c src/msx.c src/cart.c src/scc.c src/flash.c src/msxstate.c src/vsrc.c src/persist.c src/media.c src/status.c src/spcat.c src/mididev.c src/fdc.c src/fdc765.c src/ide.c src/wd33c93.c src/mb89352.c src/ym64.c src/opl4.c src/v9990.c src/gmsynth.c src/sc55_all.c src/profile.c src/devcat.c host/msx_host.c src/opll.c src/y8950.c src/y8950a.c src/netcard.c
# 저장소에는 DPC-200 롬이 없음 — 개인 시험 롬(tests/regress/local/machines/dpc200)이 있으면 그것으로
PROFILE ?= $(if $(wildcard machines/DPC-200/200bios.rom),machines/DPC-200,$(if $(wildcard tests/regress/local/machines/dpc200/200bios.rom),tests/regress/local/machines/dpc200,machines/DPC-200))
IMGPROFILE ?= machines/C-BIOS

all: build/msxrun build/cputest build/zextest build/inputtest build/carttest build/psgtest build/fattest build/growtest

build:
	mkdir -p build

build/devcattest: src/devcat.c src/devcat.h tests/devcattest.c | build
	$(CC) $(CFLAGS) -Isrc -o $@ src/devcat.c tests/devcattest.c

build/flashtest: src/flash.c src/flash.h tests/flashtest.c | build
	$(CC) $(CFLAGS) -Isrc -o $@ src/flash.c tests/flashtest.c
build/midibustest: $(CORE) tests/midibustest.c src/*.h | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/midibustest.c -lm

build/diagtest: kernel/diag.c kernel/smbios.c kernel/diag.h kernel/smbios.h tests/diagtest.c | build
	$(CC) $(CFLAGS) -o $@ kernel/diag.c kernel/smbios.c tests/diagtest.c

build/mediatest: $(CORE) tests/mediatest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -Isrc -Ihost -o $@ $(CORE) tests/mediatest.c -lm   # media.c 가 msx_dev 를 씀

build/vsrctest: $(CORE) tests/vsrctest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -Ihost -o $@ $(CORE) tests/vsrctest.c -lm

build/statustest: $(CORE) tests/statustest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -Ihost -o $@ $(CORE) tests/statustest.c -lm

build/persisttest: $(CORE) tests/persisttest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -Ihost -o $@ $(CORE) tests/persisttest.c -lm

build/sttest: $(CORE) tests/sttest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -Ihost -o $@ $(CORE) tests/sttest.c -lm

build/schedtest: $(CORE) tests/schedtest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/schedtest.c -lm
build/mpooltest: tests/mpooltest.c src/mpool.h | build
	$(CC) -O2 -std=gnu11 -Wall -Wextra -o $@ tests/mpooltest.c -lpthread

build/msxrun: $(CORE) host/headless.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) host/headless.c -lm

build/cputest: src/z80.c tests/cputest.c src/z80.h | build
	$(CC) $(CFLAGS) -o $@ src/z80.c tests/cputest.c

build/inputtest: $(CORE) kernel/input.c kernel/keymap.c kernel/kbdmaps.c tests/inputtest.c kernel/*.h src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) kernel/input.c kernel/keymap.c kernel/kbdmaps.c tests/inputtest.c -lm

build/hidpadtest: kernel/hidpad.c kernel/hidpad.h tests/hidpadtest.c | build
	$(CC) $(CFLAGS) -o $@ kernel/hidpad.c tests/hidpadtest.c

build/carttest: $(CORE) tests/carttest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/carttest.c -lm

tests/carts/expected.txt: tests/carts/make_carts.py
	python3 tests/carts/make_carts.py

build/mousetest: $(CORE) kernel/hidmouse.c kernel/dskimg.c kernel/msxfat.c kernel/hidmouse.h tests/mousetest.c | build
	$(CC) $(CFLAGS) -o $@ $(CORE) kernel/hidmouse.c kernel/dskimg.c kernel/msxfat.c tests/mousetest.c -lm

build/psgtest: src/psg.c src/psg.h tests/psgtest.c | build
	$(CC) $(CFLAGS) -o $@ src/psg.c tests/psgtest.c

FEHOST  = kernel/fe.c kernel/diag.c kernel/smbios.c kernel/keymap.c kernel/kbdmaps.c kernel/input.c kernel/machine.c kernel/medit.c kernel/sha1.c kernel/romdb.c kernel/png.c kernel/zipx.c kernel/basictxt.c kernel/jpegw.c \
          kernel/font_data.c kernel/gt_photo.c kernel/icons.c kernel/icons_hd.c kernel/hidmouse.c kernel/dskimg.c kernel/msxfat.c kernel/vfat.c kernel/badges.c kernel/hidpad.c kernel/fat.c kernel/usbgrow.c src/tinf_all.c
build/fehost: $(CORE) $(FEHOST) tests/fehost.c kernel/*.h kernel/*.inc src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -Wno-unused-function -o $@ $(CORE) $(FEHOST) tests/fehost.c -lm -lpthread

build/fehost_asan: $(CORE) $(FEHOST) tests/fehost.c kernel/*.h kernel/*.inc src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -fsanitize=address -g -D_DEFAULT_SOURCE -Wno-unused-function -o $@ $(CORE) $(FEHOST) tests/fehost.c -lm -lpthread

build/medittest: kernel/medit.c kernel/medit.h tests/medittest.c src/profile.c src/devcat.c | build
	$(CC) $(CFLAGS) -o $@ kernel/medit.c tests/medittest.c src/profile.c src/devcat.c

build/dskimgtest: kernel/dskimg.c kernel/dskimg.h tests/dskimgtest.c | build
	$(CC) $(CFLAGS) -o $@ kernel/dskimg.c tests/dskimgtest.c

build/vfattest: kernel/vfat.c kernel/vfat.h kernel/dskimg.c kernel/msxfat.c tests/vfattest.c | build
	$(CC) $(CFLAGS) -o $@ kernel/vfat.c kernel/dskimg.c kernel/msxfat.c tests/vfattest.c

build/msxfattest: kernel/dskimg.c kernel/msxfat.c kernel/msxfat.h tests/msxfattest.c | build
	$(CC) $(CFLAGS) -o $@ kernel/dskimg.c kernel/msxfat.c tests/msxfattest.c

build/midipkttest: src/midipkt.h tests/midipkttest.c | build
	$(CC) $(CFLAGS) -o $@ tests/midipkttest.c

build/fattest: kernel/fat.c kernel/fat.h tests/fattest.c | build
	$(CC) $(CFLAGS) -o $@ kernel/fat.c tests/fattest.c

build/growtest: kernel/usbgrow.c kernel/fat.c tests/growtest.c kernel/*.h | build
	$(CC) $(CFLAGS) -D_FILE_OFFSET_BITS=64 -D_POSIX_C_SOURCE=200809L -o $@ kernel/usbgrow.c kernel/fat.c tests/growtest.c

build/sccmaptest: $(CORE) tests/sccmaptest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/sccmaptest.c -lm

build/tapetest: $(CORE) tests/tapetest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/tapetest.c -lm

build/r800test: src/z80.c tests/r800test.c src/z80.h | build
	$(CC) $(CFLAGS) -o $@ src/z80.c tests/r800test.c

build/yjktest: src/vdp.c tests/yjktest.c src/vdp.h | build
	$(CC) $(CFLAGS) -o $@ src/vdp.c tests/yjktest.c

build/romdbtest: kernel/romdb.c tests/romdbtest.c | build
	$(CC) $(CFLAGS) -o $@ kernel/romdb.c tests/romdbtest.c

build/pngtest: kernel/png.c tests/pngtest.c | build
	$(CC) $(CFLAGS) -o $@ kernel/png.c src/tinf_all.c tests/pngtest.c

build/bastest: kernel/basictxt.c kernel/basictxt.h tests/bastest.c | build
	$(CC) $(CFLAGS) -o $@ kernel/basictxt.c tests/bastest.c

build/ziptest: kernel/zipx.c kernel/zipx.h tests/ziptest.c | build
	$(CC) $(CFLAGS) -o $@ kernel/zipx.c src/tinf_all.c tests/ziptest.c

build/opl4test: src/opl4.c src/opl4.h src/opl4_tab.h tests/opl4test.c | build
	$(CC) $(CFLAGS) -o $@ src/opl4.c tests/opl4test.c

build/iltest: src/vdp.c src/vdp.h tests/iltest.c | build
	$(CC) $(CFLAGS) -o $@ src/vdp.c tests/iltest.c

build/opmtest: src/ym64.c src/ym64.h src/ym64_tab.h tests/opmtest.c | build
	$(CC) $(CFLAGS) -o $@ src/ym64.c tests/opmtest.c

build/sndrstest: kernel/sndrs.h tests/sndrstest.c | build
	$(CC) $(CFLAGS) -I kernel -o $@ tests/sndrstest.c -lm

build/gmtest: src/gmsynth.c src/gmsynth.h src/gmsynth_tab.h src/sc55_all.c tests/gmtest.c | build
	$(CC) $(CFLAGS) -o $@ src/gmsynth.c src/sc55_all.c tests/gmtest.c -lm

build/fatstress: kernel/fat.c kernel/fat.h tests/fatstress.c | build
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -o $@ kernel/fat.c tests/fatstress.c

build/miditest: $(CORE) tests/miditest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/miditest.c -lm

build/msirqtest: $(CORE) tests/msirqtest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/msirqtest.c -lm
build/hbi55test: $(CORE) tests/hbi55test.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/hbi55test.c -lm
build/mfrptest: $(CORE) tests/mfrptest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/mfrptest.c -lm
build/frscctest: $(CORE) tests/frscctest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/frscctest.c -lm
build/rs232test: $(CORE) tests/rs232test.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/rs232test.c -lm
build/midijacktest: $(CORE) tests/midijacktest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/midijacktest.c -lm

build/scctest: src/scc.c src/flash.c tests/scctest.c src/scc.h | build
	$(CC) $(CFLAGS) -o $@ src/scc.c src/flash.c tests/scctest.c

build/romcheck: $(CORE) kernel/machine.c kernel/sha1.c kernel/romdb.c tests/romcheck.c src/*.h src/*.inc src/mappers/*.inc kernel/*.h | build
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -o $@ $(CORE) kernel/machine.c kernel/sha1.c kernel/romdb.c tests/romcheck.c -lm

build/machinetest: $(CORE) kernel/machine.c tests/machinetest.c src/*.h src/*.inc src/mappers/*.inc kernel/*.h | build
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -o $@ $(CORE) kernel/machine.c tests/machinetest.c -lm

build/zextest: src/z80.c tests/zextest.c src/z80.h | build
	$(CC) $(CFLAGS) -o $@ src/z80.c tests/zextest.c

tests/zex/zexdoc.com tests/zex/zexall.com:
	sh tests/zex/fetch.sh

# 빠른 시험: 타이밍 단위 시험 + DPC-200 BASIC 부팅 확인
test: build/vsrctest build/iochaintest build/memchaintest build/mediatest build/persisttest build/statustest build/devcattest build/flashtest build/diagtest build/sttest build/schedtest build/mpooltest build/midipkttest build/cputest build/msxrun build/inputtest build/carttest build/psgtest build/fattest build/scctest build/sccmaptest build/romdbtest build/pngtest build/tapetest build/yjktest build/r800test build/medittest build/hidpadtest build/mousetest build/dskimgtest build/msxfattest build/vfattest build/opl4test build/ziptest build/bastest build/opmtest build/iltest build/msirqtest build/hbi55test build/mfrptest build/frscctest build/rs232test build/midijacktest build/midibustest build/miditest build/gmtest build/sndrstest build/corefuzz build/vdpktest build/vdpktest_ms build/fatstress build/fehost_asan build/fehost tests/carts/expected.txt
	./build/cputest
	./build/sndrstest | tail -1
	./build/corefuzz 40 20000 | tail -1   # 현재 코어 = 기준 코어 (7단계 안전망)
	./build/vdpktest 20000                # 7단계: VDP 그리기 커널 C판 = 어셈블러판
	./build/vdpktest_ms                   # 같은 어셈블러를 UEFI(MS x64) 레지스터 배치로
	sh tests/initest.sh
	bash tests/spcarttest.sh   # 특수 카트리지 68종 꽂기·빼기 (5.2.86~89 에 17종이 안 꽂히던 회귀)
	bash tests/vrectest.sh     # 8.0.35 영상 녹화: 일꾼이 압축한 AVI = 메인이 혼자 압축한 AVI (바이트 단위)
	@rm -rf build/feasan_root && mkdir -p build/feasan_root/Machines && cp -r machines/C-BIOS build/feasan_root/Machines/ && ROOT=build/feasan_root MACHINE=C-BIOS W=1920 H=1080 INFO=1 INFOFILE=tests/data/diag_gram_5223.txt FEACT=diagsum START=30 KEYS=2,3,4,5,4,3,2,1,esc TAIL=60 GAP=20 ASAN_OPTIONS=halt_on_error=1:detect_leaks=0 ./build/fehost_asan > build/feasan.log 2>&1 && echo "fehost asan diag: OK" || (grep -m3 ERROR build/feasan.log; exit 1)   # 진단 창 dirty 사각형 넘침 (LG gram 멈춤) 회귀 시험
	./build/schedtest machines/C-BIOS | tail -1
	./build/mpooltest | tail -1
	./build/mediatest | tail -1
	./build/persisttest | tail -1
	./build/statustest | tail -1
	./build/vsrctest | tail -1
	python3 tests/menudlgtest.py   # 메뉴에서 연 창(164항목)을 키로 움직여도 화면이 '전부 다시 그린 것'과 같음 (미디어 슬롯 삽입 창이 깨지던 것)
	./tests/dckeytest.sh
	./build/memchaintest | diff - tests/data/memchain_expected.txt && echo "memchain: OK"   # 메모리 칸 사슬 동등성
	./build/iochaintest | diff - tests/data/iochain_expected.txt && echo "iochain: OK"   # I/O 사슬 동등성 (DeviceOps 표)
	./build/sttest | tail -1
	./build/diagtest | tail -1
	./build/flashtest
	./build/devcattest
	./build/bastest
	@sh tests/ziptest.sh | tail -1
	./build/opl4test | tail -1
	./build/opmtest | tail -1
	@sh tests/vfattest.sh | tail -1
	./build/iltest | tail -1
	./build/msirqtest $(PROFILE) | tail -1
	./build/hbi55test $(PROFILE) | tail -1
	./build/mfrptest | tail -1
	./build/frscctest | tail -1
	./build/rs232test $(PROFILE) | tail -1
	./build/midijacktest $(PROFILE) | tail -1
	./build/midibustest | tail -1
	./build/miditest $(PROFILE) | tail -1
	./build/gmtest | tail -1
	@sh tests/fatstress.sh
	./build/psgtest
	./build/mousetest
	@sh tests/msxfat.sh
	@bash tests/growtest.sh
	./build/scctest | tail -1
	./build/inputtest $(PROFILE)
	./build/carttest $(PROFILE)
	./build/sccmaptest $(PROFILE) | tail -1
	./build/tapetest $(PROFILE) | tail -1
	./build/yjktest | tail -1
	./build/r800test | tail -1
	./build/hidpadtest | tail -1
	./build/midipkttest | tail -1
	./build/romdbtest | tail -1
	./build/medittest machines/*/config.ini | tail -1
	@python3 tests/mkpngtests.py && ./build/pngtest $$(cat build/pngt/args.txt) | tail -1
	@sh tests/mkfat16.sh build/fat16.img
	@if [ -f build/msx1.img ]; then MTOOLS_SKIP_CHECK=1 mtype -i build/msx1.img@@1M ::/X86MSX.BIN > build/img_kernel.bin && ./build/fattest; fi
	@fsck.fat -n build/fat16.img >/dev/null && echo "fat: fsck clean"
	./build/msxrun $(PROFILE) 400 --text | grep -q "Bytes free" && echo "boot: BASIC prompt OK"
	@AUDIO=1 ./build/msxrun $(PROFILE) 2000 --type 420 '10 for r=0 to 10:read a,b:out &hc0,a:out &hc1,b:next\n' --type 900 '20 data 32,33,35,33,64,63,67,0,96,240,99,240,128,15,131,15,160,152,176,49,0,0\n' --type 1500 'run\n' --wav build/y8950.wav > /dev/null && python3 tests/wavfreq.py build/y8950.wav 27 31 | grep -q "^30[0-9]" && echo "y8950: MSX-AUDIO FM tone OK"

# 5.0.185 회귀 시험 묶음 (구조 개편 0단계): 기종별 부팅·카트·소리·Go Figure 화면/소리 해시 비교 + 속도
fuzzcmp:                                     # make fuzzcmp OLD=<이전 판 소스> — 구조 개편 동등성 무작위 비교
	tests/fuzz/fuzzcmp.sh $(OLD)

regress: build/msxrun
	python3 tests/regress.py
# 판올림 전 확인: 단위 시험 + 회귀 시험
check: test regress
regress-update: build/msxrun
	python3 tests/regress.py --update

# Z80 명령 시험 (각 1분 내외)
zex: build/zextest tests/zex/zexdoc.com tests/zex/zexall.com
	./build/zextest tests/zex/zexdoc.com
	./build/zextest tests/zex/zexall.com

clean:
	rm -rf build

.PHONY: all test zex clean regress regress-update regress-asm check

# ---------------------------------------------------------------------
# 베어메탈 부팅 이미지 (레거시 BIOS/CSM, 대상: Acer Aspire V5-171)
#   make image   → build/msx1.img (64 MiB: 로더·본체 + FAT32 파티션에 $(PROFILE)의 ROM과 설정)
# ---------------------------------------------------------------------
KCC     = gcc
KFLAGS  = -m32 -march=i686 -O2 -std=c11 -Wall -Wextra -ffreestanding -fno-pic -fno-pie \
          -fno-stack-protector -fno-asynchronous-unwind-tables -mgeneral-regs-only \
          --param=min-pagesize=0 -nostdinc -isystem $(shell $(KCC) -print-file-name=include) -Ikernel/include
NETSRC  = $(wildcard third_party/lwip/src/core/*.c) $(wildcard third_party/lwip/src/core/ipv4/*.c) third_party/lwip/src/netif/ethernet.c kernel/net/netstack.c kernel/net/nethost_lwip.c kernel/net/nicdb.c kernel/net/nic_bcm57785.c
NETOBJ  = $(patsubst %.c,build/net/%.o,$(NETSRC))
NETFLAGS = $(KFLAGS) -Ikernel/net -Ithird_party/lwip/src/include -Wno-unused-parameter -Wno-type-limits -Wno-sign-compare
build/net/%.o: %.c kernel/net/lwipopts.h kernel/net/arch/cc.h
	@mkdir -p $(dir $@)
	$(KCC) $(NETFLAGS) -c $< -o $@

KSRC    = src/z80.c src/vdp.c src/psg.c src/msx.c src/cart.c src/netcard.c src/scc.c src/flash.c src/msxstate.c src/vsrc.c src/persist.c src/media.c src/status.c src/spcat.c src/mididev.c src/fdc.c src/fdc765.c src/ide.c src/wd33c93.c src/mb89352.c src/ym64.c src/opl4.c src/v9990.c src/gmsynth.c src/sc55_all.c kernel/kmain.c kernel/acpi.c kernel/fe.c kernel/diag.c kernel/smbios.c kernel/keymap.c kernel/kbdmaps.c kernel/input.c kernel/hda.c kernel/bios.c kernel/fat.c kernel/usbgrow.c kernel/machine.c src/profile.c src/devcat.c kernel/font_data.c kernel/gt_photo.c kernel/icons.c kernel/icons_hd.c kernel/hidmouse.c kernel/dskimg.c kernel/msxfat.c kernel/vfat.c kernel/badges.c kernel/medit.c kernel/sha1.c kernel/romdb.c kernel/png.c kernel/zipx.c kernel/basictxt.c kernel/jpegw.c src/tinf_all.c kernel/libc.c kernel/amddet.c
ROMS   ?= machines/DPC-200/200bios.rom machines/DPC-200/200han.rom

build/loader.bin: boot/loader.asm boot/mbr.inc | build
	nasm -f bin -w+all -i boot/ boot/loader.asm -o $@

build/thunk.bin: boot/thunk.asm | build
	nasm -f bin -w+all boot/thunk.asm -o $@

build/kernel.elf: $(KSRC) $(NETOBJ) kernel/entry.asm kernel/link.ld src/*.h src/*.inc src/mappers/*.inc kernel/*.h kernel/*.inc build/thunk.bin | build
	nasm -f elf32 kernel/entry.asm -o build/entry.o
	$(KCC) $(KFLAGS) -c $(KSRC)
	$(KCC) $(subst -mgeneral-regs-only,-mfpmath=387 -mno-sse,$(KFLAGS)) -c src/opll.c src/y8950.c src/y8950a.c kernel/libm.c
	mv -f z80.o vdp.o psg.o msx.o cart.o netcard.o scc.o flash.o msxstate.o vsrc.o persist.o media.o status.o spcat.o mididev.o devcat.o fdc.o fdc765.o ide.o wd33c93.o mb89352.o ym64.o opl4.o v9990.o gmsynth.o sc55_all.o kmain.o acpi.o fe.o diag.o smbios.o keymap.o kbdmaps.o input.o hda.o bios.o fat.o usbgrow.o machine.o profile.o font_data.o gt_photo.o icons.o icons_hd.o hidmouse.o dskimg.o msxfat.o vfat.o badges.o medit.o sha1.o romdb.o png.o zipx.o basictxt.o jpegw.o tinf_all.o libc.o opll.o y8950.o y8950a.o libm.o amddet.o build/
	ld -m elf_i386 -T kernel/link.ld -nostdlib -z noexecstack --no-warn-rwx-segments -o $@ build/entry.o \
	   build/z80.o build/vdp.o build/psg.o build/msx.o build/cart.o build/netcard.o build/scc.o build/flash.o build/msxstate.o build/vsrc.o build/persist.o build/media.o build/status.o build/spcat.o build/mididev.o build/devcat.o build/fdc.o build/fdc765.o build/ide.o build/wd33c93.o build/mb89352.o build/ym64.o build/opl4.o build/v9990.o build/gmsynth.o build/sc55_all.o build/kmain.o build/acpi.o build/fe.o build/diag.o build/smbios.o build/keymap.o build/kbdmaps.o build/input.o build/hda.o build/bios.o build/fat.o build/usbgrow.o build/machine.o build/profile.o build/font_data.o build/gt_photo.o build/icons.o build/icons_hd.o build/hidmouse.o build/dskimg.o build/msxfat.o build/vfat.o build/badges.o build/medit.o build/sha1.o build/romdb.o build/png.o build/zipx.o build/basictxt.o build/jpegw.o build/tinf_all.o build/libc.o build/opll.o build/y8950.o build/y8950a.o build/libm.o build/amddet.o $(NETOBJ)

build/kernel.bin: build/kernel.elf
	@end=$$(nm $< | awk '$$3=="__bss_end"{print $$1}'); \
	 if [ $$((0x$$end)) -ge $$((0x03800000)) ]; then echo "kernel BSS end 0x$$end overlaps fixed memory (kernel/memmap.h)"; exit 1; fi
	objcopy -O binary $< $@
	@sz=$$(stat -c %s $@); if [ $$sz -gt 8388608 ]; then echo "X86MSX.BIN $$sz bytes > 8 MiB loader limit"; rm -f $@; exit 1; fi

build/msx1.img: build/loader.bin build/kernel.bin build/BOOTX64.EFI tools/mkimage.py third_party/timgm6mb/TimGM6mb.sf2 $(IMGPROFILE)/config.ini tests/carts/expected.txt
	python3 tools/mkimage.py $@ build/loader.bin build/kernel.bin $(IMGPROFILE) --test-carts tests/carts --efi build/BOOTX64.EFI

image: build/msx1.img

.PHONY: image

# ---------------------------------------------------------------------
# UEFI 버전: \EFI\BOOT\BOOTX64.EFI (x86_64, clang + lld-link)
# ---------------------------------------------------------------------
UCC     = clang
UFLAGS  = -target x86_64-unknown-windows -O2 -gcodeview -g -std=c11 -Wall -Wextra -ffreestanding -fshort-wchar -DFE_UEFI -DVDPK_ASM \
          -mno-red-zone -fno-stack-protector -mno-stack-arg-probe -fno-builtin-memset \
          -nostdinc -isystem $(shell $(UCC) -print-resource-dir)/include -Ikernel/include
USRC    = src/z80.c src/vdp.c src/psg.c src/msx.c src/cart.c src/scc.c src/flash.c src/msxstate.c src/vsrc.c src/persist.c src/media.c src/status.c src/spcat.c src/mididev.c src/fdc.c src/fdc765.c src/ide.c src/wd33c93.c src/mb89352.c src/ym64.c src/opl4.c src/v9990.c src/gmsynth.c src/sc55_all.c kernel/fe.c kernel/diag.c kernel/smbios.c kernel/keymap.c kernel/kbdmaps.c kernel/input.c \
          kernel/hda.c kernel/machine.c kernel/usbgrow.c kernel/fat.c src/profile.c src/devcat.c kernel/font_data.c kernel/gt_photo.c kernel/icons.c kernel/icons_hd.c kernel/hidmouse.c kernel/dskimg.c kernel/msxfat.c kernel/vfat.c kernel/badges.c kernel/medit.c kernel/sha1.c kernel/romdb.c kernel/png.c kernel/zipx.c kernel/basictxt.c kernel/jpegw.c src/tinf_all.c kernel/libc.c kernel/libm.c src/opll.c src/y8950.c src/y8950a.c src/netcard.c uefi/netuefi.c uefi/main.c uefi/usbkbd.c uefi/usbpad.c uefi/usbmouse.c uefi/i2cdiag.c uefi/rtwdet.c uefi/usbnic_r8152.c uefi/usbnic_asix.c uefi/wpa.c uefi/amdedid.c uefi/amddisp.c uefi/intelhdmi.c uefi/ivbhdmi.c uefi/amdhdmi.c uefi/hdadiag.c uefi/vtd.c uefi/mpwake.c uefi/vfctdet.c uefi/usbmidi.c kernel/hidpad.c kernel/amddet.c

# UEFI 판에도 lwIP + netstack(무선 Nic 연결) — 무선 PC(1번 Lenovo V14-ARE)용. 설정은 kernel/net 것을 같이 씀
UNETSRC = $(wildcard third_party/lwip/src/core/*.c) $(wildcard third_party/lwip/src/core/ipv4/*.c) third_party/lwip/src/netif/ethernet.c kernel/net/netstack.c kernel/net/nethost_lwip.c kernel/net/nicdb.c
UNETFLAGS = -I../../kernel/net -I../../third_party/lwip/src/include -Wno-unused-parameter -Wno-sign-compare -Wno-type-limits -Wno-unused-function -Wno-pointer-sign
# Roland MT-32 / CM-32L 음원 (munt mt32emu, LGPL-2.1+, C++). 예외·RTTI·libm 없이 UEFI 판에만 (레거시 판은 해당 없음)
UMT     = $(filter-out third_party/mt32emu/c_interface%,$(wildcard third_party/mt32emu/*.cpp)) third_party/mt32emu/sha1/sha1.cpp
UCXXFLAGS = -target x86_64-unknown-windows -O2 -std=c++11 -ffreestanding -fshort-wchar -DFE_UEFI -mno-red-zone -fno-stack-protector -mno-stack-arg-probe \
          -fno-exceptions -fno-rtti -fno-threadsafe-statics -nostdinc -nostdinc++ -isystem $(shell $(UCC) -print-resource-dir)/include \
          -I../../third_party/mt32emu/shim -I../../kernel/include -I../../third_party/mt32emu -I../../third_party/mt32emu/sha1 -w
build/BOOTX64.EFI: build/vdpk_x64_win.obj uefi/mpwake_tramp.inc $(USRC) $(UNETSRC) $(UMT) third_party/mt32emu/*.h third_party/mt32emu/*.inc uefi/*.h kernel/*.h kernel/*.inc src/*.h src/*.inc src/mappers/*.inc kernel/net/*.h | build
	mkdir -p build/uefi && rm -f build/uefi/*.o   # 3.7.0: 지운 소스의 옛 .o가 링크되지 않게
	cd build/uefi && for f in $(USRC); do $(UCC) $(UFLAGS) -I../../kernel/include -c ../../$$f -o $$(basename $$f .c).o || exit 1; done
	cd build/uefi && for f in $(UMT); do clang++ $(UCXXFLAGS) -c ../../$$f -o mt_$$(basename $$f .cpp).o || exit 1; done
	cd build/uefi && for f in $(UNETSRC); do $(UCC) $(UFLAGS) -I../../kernel/include $(UNETFLAGS) -c ../../$$f -o $$(basename $$f .c).o || exit 1; done
	lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /map:build/BOOTX64.map /debug /pdb:build/BOOTX64.pdb /out:$@ build/uefi/*.o build/vdpk_x64_win.obj

# 8.0.6: 깨운 코어가 처음 실행하는 코드 (NASM → C 배열). 생성물 uefi/mpwake_tramp.inc 도 소스에 같이 둠 (nasm 없는 곳에서도 빌드되게)
uefi/mpwake_tramp.inc: uefi/mpwake_tramp.asm
	nasm -f bin -w+all $< -o build/mpwake_tramp.bin
	python3 tools/bin2c.py build/mpwake_tramp.bin 2192 mpwake_tramp > $@

efi: build/BOOTX64.EFI

.PHONY: efi

# 7단계: 어셈블러 부품 (NASM, x86-64). 호스트는 elf64(System V), UEFI는 win64. C판이 기준이고 기본으로 쓰임 (src/vdpk.h)
build/vdpk_x64.o: src/asm/vdpk_x64.asm | build
	nasm -f elf64 $< -o $@
build/vdpk_x64_win.obj: src/asm/vdpk_x64.asm | build
	nasm -f win64 $< -o $@
build/vdpk_ms.o: src/asm/vdpk_x64.asm | build
	nasm -f elf64 -DFORCE_MSABI $< -o $@
build/vdpktest_ms: tests/vdpktest_ms.c src/vdpk.inc src/vdpk.h build/vdpk_ms.o | build
	$(CC) -O2 -std=c11 -Wall -Wextra -x c src/vdpk.inc -x none -o $@ tests/vdpktest_ms.c build/vdpk_ms.o -Wl,-z,noexecstack
build/vdpktest: tests/vdpktest.c src/vdpk.inc src/vdpk.h build/vdpk_x64.o | build
	$(CC) -O2 -std=c11 -Wall -Wextra -DVDPK_ASM -x c src/vdpk.inc -x none -o $@ tests/vdpktest.c build/vdpk_x64.o -Wl,-z,noexecstack
# 어셈블러판을 켜서(VDPASM=1) 같은 회귀 시험을 돌리는 msxrun:  MSXRUN=build/msxrun_asm VDPASM=1 python3 tests/regress.py
build/msxrun_asm: $(CORE) host/headless.c src/*.h src/*.inc src/mappers/*.inc build/vdpk_x64.o | build
	$(CC) $(CFLAGS) -DVDPK_ASM -o $@ $(CORE) host/headless.c build/vdpk_x64.o -lm -Wl,-z,noexecstack
regress-asm: build/msxrun_asm
	MSXRUN=build/msxrun_asm VDPASM=1 python3 tests/regress.py

build/memchaintest: $(CORE) tests/memchaintest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -Ihost -o $@ $(CORE) tests/memchaintest.c -lm
build/iochaintest: $(CORE) tests/iochaintest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -Ihost -o $@ $(CORE) tests/iochaintest.c -lm

# Z80/R800 코어 동등성 시험 (7단계 안전망) — 현재 코어(src/z80.c) 대 동결 기준 코어(tests/ref/z80_ref.c)
COREFUZZ_REN = -Dz80_init=z80r_init -Dz80_reset=z80r_reset -Dz80_step=z80r_step
build/corefuzz: tests/corefuzz/corefuzz.c tests/corefuzz/cf_wrap.c tests/corefuzz/cf.h src/z80.c src/z80.h tests/ref/z80_ref.c tests/ref/z80_ref.h | build
	$(CC) -O2 -std=c11 -w -Isrc -DCF_PFX=cur_ -DCF_HDR='"z80.h"' -c tests/corefuzz/cf_wrap.c -o build/cf_cur.o
	$(CC) -O2 -std=c11 -w -Itests/ref $(COREFUZZ_REN) -DCF_REF -DCF_PFX=ref_ -DCF_HDR='"z80_ref.h"' -c tests/corefuzz/cf_wrap.c -o build/cf_ref.o
	$(CC) -O2 -std=c11 -w -Isrc -c src/z80.c -o build/cf_z80.o
	$(CC) -O2 -std=c11 -w -Itests/ref $(COREFUZZ_REN) -c tests/ref/z80_ref.c -o build/cf_z80r.o
	$(CC) -O2 -std=c11 -w -Itests/corefuzz -o $@ tests/corefuzz/corefuzz.c build/cf_cur.o build/cf_ref.o build/cf_z80.o build/cf_z80r.o

build/mpmiditest: $(CORE) tests/mpmiditest.c src/*.h src/*.inc src/mappers/*.inc | build
	$(CC) $(CFLAGS) -o $@ $(CORE) tests/mpmiditest.c -lm -lpthread
