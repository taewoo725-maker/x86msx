#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mkimage.py - 부팅 이미지 조립 (설계서 §8)

배치
  LBA 0          MBR (loader.bin 첫 섹터) + FAT32 파티션 표
  LBA 1~         2단계 로더                              ← 파티션 앞 빈 공간 (Windows에서 안 보임)
  LBA 2048~      FAT32 파티션 "X86MSX"                  ← Windows에서 보임
                   \\EFI\\BOOT\\BOOTX64.EFI                UEFI 부팅용 에뮬레이터 (x86_64)
                   \\X86MSX.BIN                          레거시(CSM) 부팅용 에뮬레이터 본체
                   \\MACHINES\\MSX - C-BIOS\\CONFIG.INI, CBIOS_*.ROM
                   \\Storage Device\\ROMS\\                게임 카트리지 (*.ROM)
                   \\README.txt

사용법: mkimage.py out.img loader.bin kernel.bin profile_dir [--no-roms] [--test-carts DIR] [--efi BOOTX64.EFI]
"""
ROOT = __import__("os").path.dirname(__import__("os").path.dirname(__import__("os").path.abspath(__file__)))
import os, struct, subprocess, sys, tempfile

IMAGE_MIB = 64
PART_LBA = 2048
KERNEL_LOAD = 0x100000

README = """X86MSX - MSX 에뮬레이터 부팅 USB
====================================

이 USB로 PC를 켜면 X86MSX가 시작되어 마지막으로 쓰던 기종으로 부팅합니다.
처음 기종은 C-BIOS(무료 대체 BIOS)입니다. C-BIOS에는 BASIC이 없고 카트리지(롬)를 넣으면 실행합니다.
BASIC·디스크가 필요하면 본인이 가진 기종의 ROM을 Machines\\<기종 이름>\\ 에 넣으세요 (상용 ROM은 들어 있지 않습니다).
  UEFI 부팅: EFI\\BOOT\\BOOTX64.EFI (펌웨어 설정에서 Secure Boot 끄기)
  레거시(CSM) 부팅: X86MSX.BIN (메모리 118 MB 이상)

폴더
  Machines\\<기종 이름>\\    기종 프로필 (blueMSX의 Machines 폴더와 같은 구조)
  Machines\\Shared Roms\\    blueMSX의 Machines\\Shared Roms를 그대로 복사해 넣는 곳
  Databases\\              blueMSX 롬 데이터베이스 (롬 형식 자동 판별)
  Storage Device\\         MSX에 넣어 쓰는 파일을 모아 두는 곳 (아래 폴더가 모두 여기에 있음)
    ROMS\\  DISK\\  CAS\\  HARDDISK\\   롬팩, 디스크, 테이프, 하드 디스크 이미지
    DISK\\DirFolder\\       이미지 파일 관리의 USB 쪽 기본 폴더 (DISK\\TEMP는 임시 폴더)
    VMFolder\\             가상 하드: 여기 만든 폴더를 MSX 하드 디스크 C~H로 (파일 → 미디어 → 하드 디스크 → C:~H:)
      MSXDOSA\\    MSXDOS.SYS·COMMAND.COM을 넣으면 MSX-DOS1로 부팅 (FAT12, 15 MB까지)
      MSXDOS2A\\   MSXDOS2.SYS·COMMAND2.COM을 넣으면 MSX-DOS2로 부팅 (Nextor 커널, FAT16)
      NEXTORA\\    NEXTOR.SYS(들어 있음)와 COMMAND2.COM을 넣으면 Nextor로 부팅
    BASIC\\                BASIC 텍스트(TXT) 넣기·저장 (파일 메뉴)
  (예전 버전의 최상위 ROMS·DISK·CAS·HARDDISK·BASIC·VMFolder 폴더는 Storage Device 안으로 옮겨 두세요. 설정(ini)의 옛 경로는 자동으로 바뀝니다.)
  SRAM\\                   카트리지 SRAM, CMOS 백업
  States\\                 상태 저장 (F8 저장, F7 불러오기)
  Screenshots\\  Audio\\  Video\\   화면 저장, 소리 저장, 영상 녹화
  MidiSound\\SoundFont\\  MIDI 음원 .sf2 (TimGM6mb.sf2 기본 포함) / MidiSound\\MT32\\ MT-32 롬 / MidiSound\\SC55\\ SC-55 롬 (5.2.61)
  Printer\\               프린터 출력 (TXT·PRN)
  Icons\\                  상태 표시 아이콘·기종 배지 그림 (바꿔 쓸 수 있음, Icons\\README.txt)
  Keyboard Config\\        키보드 설정
  x86msx.ini              설정과 마지막 상태 (자동으로 씀)

메뉴는 F11, 단축키는 blueMSX와 같습니다. 모든 창은 키보드로 조작합니다:
  Tab / Shift+Tab 탭 바꾸기, 숫자 키 탭, ↑/↓ 항목, ←/→ 값, Enter 고르기·목록, Esc 닫기
  창 아래 명령 줄에 그 창에서 쓰는 키가 나옵니다.

큰 USB 메모리에 이 이미지를 기록하면 처음에는 64 MB만 쓰입니다.
X86MSX에서 F11 → 도움말 → "USB 전체 용량 사용"을 한 번 실행하면
파일은 그대로 두고 USB 전체 크기로 늘린 뒤 다시 시작합니다.
라이선스: X86MSX는 GNU GPL v3 자유 소프트웨어입니다 (LICENSE.txt). 제3자 소프트웨어와 자료는
THIRD-PARTY-NOTICES.txt, LICENSE-Nextor.txt를 보세요. 본인이 넣은 ROM은 본인 기기에서만 쓰고 배포하지 마세요.
"""


VM_README = """X86MSX 가상 하드 (VMFolder)
==========================

이 폴더 안에 만든 폴더를 MSX에서 하드 디스크로 씁니다. 이미지 파일은 만들지 않고,
폴더 안 파일이 그대로 MSX 하드의 파일로 보입니다. MSX에서 쓴 내용은 2초 뒤 이 폴더에 반영됩니다.

연결: F11 → 파일 → 미디어 → 하드 디스크 → C:~H: 를 고르고, 폴더에서 Ins → 용량·형식 → 저장.
  (설정은 그 폴더 안 X86MSX.VHD 파일에 저장, MSX에는 보이지 않음)
  A·B = 플로피, C~H = 가상 하드 6개. 커널은 Nextor 2.1.3 (Machines\\Shared Roms\\Nextor-2.1.3.base.dat).

부팅 세트 (시스템 파일은 가지고 계신 것을 복사해 넣으세요)
  MSXDOSA\\    MSXDOS.SYS, COMMAND.COM            → MSX-DOS1 (FAT12, 15 MB까지.
                                                    플로피 A가 비어 있으면 이 하드가 A:로 부팅, 플로피 A는 B:)
  MSXDOS2A\\   MSXDOS2.SYS, COMMAND2.COM          → MSX-DOS2 (Nextor 커널 위에서 FAT16)
  NEXTORA\\    NEXTOR.SYS(들어 있음), COMMAND2.COM → Nextor

MSX 이름은 8.3 (긴 이름은 NAME~1.EXT로 보임). 하드 하나에 파일 4096개·폴더 512개까지.
"""

def pad(b):
    return b + b'\0' * (-len(b) % 512)


def chs(lba):
    spt, heads = 63, 255
    c = lba // (spt * heads)
    if c > 1023:
        return bytes([0xFE, 0xFF, 0xFF])
    h = (lba // spt) % heads
    s = lba % spt + 1
    return bytes([h, s | ((c >> 2) & 0xC0), c & 0xFF])


def run(*args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL)


def main():
    argv = sys.argv[1:]
    test_carts = None
    efi = None
    if '--efi' in argv:
        i = argv.index('--efi')
        efi = argv[i + 1]
        del argv[i:i + 2]
    if '--test-carts' in argv:
        i = argv.index('--test-carts')
        test_carts = argv[i + 1]
        del argv[i:i + 2]
    args = [a for a in argv if not a.startswith('--')]
    no_roms = '--no-roms' in argv
    if len(args) != 4:
        sys.exit(__doc__)
    out, loader_p, kernel_p, profile = args
    loader = open(loader_p, 'rb').read()
    kernel = open(kernel_p, 'rb').read()
    assert len(loader) % 512 == 0 and loader[510:512] == b'\x55\xaa', 'bad loader'

    # 파티션 앞 영역: MBR + 2단계 로더
    head = bytearray(loader)
    if len(head) > PART_LBA * 512:
        sys.exit('loader too large for the area before the partition')
    if len(kernel) > 0x800000:                                  # 로더 KERNEL_MAX (3.0부터 8 MiB)
        sys.exit('kernel larger than 8 MiB')

    total = IMAGE_MIB * 2048
    part_sectors = total - PART_LBA

    # MBR 파티션 표: FAT32 LBA(0x0C), 활성
    entry = bytes([0x80]) + chs(PART_LBA) + bytes([0x0C]) + chs(total - 1) + struct.pack('<II', PART_LBA, part_sectors)
    head[0x1BE:0x1CE] = entry
    head[0x1CE:0x1FE] = b'\0' * 48

    # FAT32 파티션 만들기
    with tempfile.TemporaryDirectory() as tmp:
        part = os.path.join(tmp, 'part.img')
        with open(part, 'wb') as f:
            f.truncate(part_sectors * 512)
        run('mkfs.fat', '-F', '32', '-s', '1', '-S', '512', '-h', str(PART_LBA), '-n', 'X86MSX', part)
        env = dict(os.environ, MTOOLS_SKIP_CHECK='1')
        def mt(*a):
            subprocess.run(a, check=True, env=env, stdout=subprocess.DEVNULL)
        mt('mcopy', '-i', part, kernel_p, '::/X86MSX.BIN')
        if efi:
            mt('mmd', '-i', part, '::/EFI')
            mt('mmd', '-i', part, '::/EFI/BOOT')
            mt('mcopy', '-i', part, efi, '::/EFI/BOOT/BOOTX64.EFI')
        machine = 'MSX - C-BIOS'   # 4.0: 기본 기종 = C-BIOS (BSD 라이선스, 재배포 가능). 상용 기종 ROM은 이미지에 넣지 않음
        for d in ['Machines', 'Machines/' + machine, 'Machines/Shared Roms', 'Keyboard Config',
                  'Storage Device', 'Storage Device/CAS', 'Storage Device/DISK', 'Storage Device/DISK/DirFolder', 'Storage Device/DISK/TEMP',
                  'Storage Device/ROMS', 'Storage Device/HARDDISK', 'Storage Device/BASIC',
                  'SRAM', 'States', 'Screenshots', 'Audio', 'Video', 'Printer', 'Databases', 'MidiSound', 'MidiSound/SoundFont', 'MidiSound/MT32', 'MidiSound/SC55',   # MIDI 음원 파일은 MidiSound 아래
                  'Storage Device/VMFolder', 'Storage Device/VMFolder/MSXDOSA', 'Storage Device/VMFolder/MSXDOS2A', 'Storage Device/VMFolder/NEXTORA']:   # 3.7.1: 가상 하드, 4.0.13: Storage Device 아래
            mt('mmd', '-i', part, '::/' + d)
        files = ['config.ini'] + ([] if no_roms else sorted(f for f in os.listdir(profile) if f.lower().endswith('.rom') or f.startswith('LICENSE')))
        for name in files:
            mt('mcopy', '-i', part, os.path.join(profile, name), '::/Machines/' + machine + '/' + name)
        if test_carts:
            names = {'konami.rom': 'TEST-Konami mapper.ROM', 'konamiscc.rom': 'TEST-Konami SCC mapper.ROM',
                     'ascii8.rom': 'TEST-ASCII8 mapper.ROM', 'ascii16.rom': 'TEST-ASCII16 mapper.ROM',
                     'plain16.rom': 'TEST-Plain 16K.ROM', 'plain32.rom': 'TEST-Plain 32K.ROM'}
            for src, dst in names.items():
                mt('mcopy', '-i', part, os.path.join(test_carts, src), '::/Storage Device/ROMS/' + dst)
        mt('mmd', '-i', part, '::/fonts')                                       # 부드러운 글꼴 파일 (옵션 > 언어 > 글꼴 바꾸기)
        for fsrc, fdst in (('noto/NotoSansKR.x8f', 'NotoSansKR.x8f'), ('noto/LICENSE.txt', 'LICENSE-NotoSansCJK.txt'), ('gothic/GothicKR.x8f', 'GothicKR.x8f'), ('gothic/OFL.txt', 'LICENSE-GothicKR.txt'), ('happy/HappyKR.x8f', 'HappyKR.x8f'), ('happy/README.txt', 'LICENSE-HappyKR.txt')):
            fp = os.path.join(ROOT, 'third_party', fsrc)
            if os.path.exists(fp): mt('mcopy', '-i', part, fp, '::/fonts/' + fdst)
        lic = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'third_party', 'galmuri', 'LICENSE.txt')
        if os.path.exists(lic):
            mt('mcopy', '-i', part, lic, '::/LICENSE-Galmuri-font.txt')
        for src_, dst_ in (('LICENSE', 'LICENSE.txt'), ('THIRD-PARTY-NOTICES.txt', 'THIRD-PARTY-NOTICES.txt')):   # 4.0: GPL v3 전문, 제3자 고지
            if os.path.exists(os.path.join(ROOT, src_)):
                mt('mcopy', '-i', part, os.path.join(ROOT, src_), '::/' + dst_)
        lic2 = os.path.join(ROOT, 'third_party', 'dejavu', 'LICENSE.txt')     # 3.4.3: 아랍어 글자
        if os.path.exists(lic2):
            mt('mcopy', '-i', part, lic2, '::/LICENSE-DejaVu-font.txt')
        unapi = os.path.join(ROOT, 'assets', 'net', 'unapi.rom')       # 네트워크 카트리지 UNAPI ROM (자체 제작)
        if os.path.exists(unapi):
            mt('mcopy', '-i', part, unapi, '::/Machines/Shared Roms/UNAPI.ROM')
        nxdir = os.path.join(ROOT, 'third_party', 'nextor')            # 3.7.1: Nextor 2.1.3 (무료 배포, LICENSE.md) — 가상 하드 커널·NEXTOR.SYS
        if os.path.exists(os.path.join(nxdir, 'Nextor-2.1.3.base.dat')):
            mt('mcopy', '-i', part, os.path.join(nxdir, 'Nextor-2.1.3.base.dat'), '::/Machines/Shared Roms/Nextor-2.1.3.base.dat')
            mt('mcopy', '-i', part, os.path.join(nxdir, 'NEXTOR.SYS'), '::/Storage Device/VMFolder/NEXTORA/NEXTOR.SYS')
            mt('mcopy', '-i', part, os.path.join(nxdir, 'LICENSE-Nextor.txt'), '::/LICENSE-Nextor.txt')   # 4.0.11: 받은 원문 그대로
            vr = os.path.join(tmp, 'vmreadme.txt')
            with open(vr, 'wb') as f:
                f.write(VM_README.replace('\n', '\r\n').encode('utf-8-sig'))
            mt('mcopy', '-i', part, vr, '::/Storage Device/VMFolder/README.txt')
        sfdir = os.path.join(ROOT, 'third_party', 'timgm6mb')        # 3.6.2: 기본 SoundFont (TimGM6mb, GPL-2, 자유 배포)
        if os.path.exists(os.path.join(sfdir, 'TimGM6mb.sf2')):
            mt('mcopy', '-i', part, os.path.join(sfdir, 'TimGM6mb.sf2'), '::/MidiSound/SoundFont/TimGM6mb.sf2')
            mt('mcopy', '-i', part, os.path.join(sfdir, 'COPYRIGHT.txt'), '::/MidiSound/SoundFont/TimGM6mb-COPYRIGHT.txt')
            mt('mcopy', '-i', part, os.path.join(sfdir, 'GPL-2.txt'), '::/MidiSound/SoundFont/TimGM6mb-GPL-2.txt')
        dbdir = os.path.join(ROOT, 'third_party', 'databases')     # blueMSX 데이터베이스 (사용자 제공)
        if os.path.isdir(dbdir):
            for f in sorted(os.listdir(dbdir)):
                mt('mcopy', '-i', part, os.path.join(dbdir, f), '::/Databases/' + f)
            zdb = os.path.join(dbdir, 'xml-msxromsdb.zip')         # 3.2.15: openMSX softwaredb → X86MSX 이진 롬 DB
            if os.path.exists(zdb):
                tb = os.path.join(tempfile.gettempdir(), 'x86msx_romdb.bin')
                subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mkromdb.py'), zdb, tb], check=True, stdout=subprocess.DEVNULL)
                mt('mcopy', '-i', part, tb, '::/Databases/x86msx_romdb.bin')
            cdb = os.path.join(dbdir, 'msxcaswavdb.xml')           # 3.3.2 MED-08: 카세트 DB → 이진
            if os.path.exists(cdb):
                tc = os.path.join(tempfile.gettempdir(), 'x86msx_casdb.bin')
                subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mkcasdb.py'), cdb, tc], check=True, stdout=subprocess.DEVNULL)
                mt('mcopy', '-i', part, tc, '::/Databases/x86msx_casdb.bin')
        # 상태 표시 아이콘·기종 배지 (3.0: 본체와 같은 그림을 넣어 두고, 사용자가 바꾸면 그 그림을 씀)
        mt('mmd', '-i', part, '::/Icons')
        for sub in ('icons', 'badges'):
            d_ = os.path.join(ROOT, 'assets', sub)
            for f in sorted(os.listdir(d_)) if os.path.isdir(d_) else []:
                mt('mcopy', '-i', part, os.path.join(d_, f), '::/Icons/' + f)
        mt('mmd', '-i', part, '::/Icons/Keyboard')                   # 기종 키보드 그림 (키 편집기)
        kd_ = os.path.join(ROOT, 'assets', 'keyboard')
        for f in sorted(os.listdir(kd_)) if os.path.isdir(kd_) else []:
            mt('mcopy', '-i', part, os.path.join(kd_, f), '::/Icons/Keyboard/' + f)
        readme = os.path.join(tmp, 'README.txt')                     # 3.0.1: assets/icons_readme.txt (CRLF, BOM)
        with open(readme, 'wb') as rf:
            rf.write(open(os.path.join(ROOT, 'assets', 'icons_readme.txt'), encoding='utf-8').read().replace('\n', '\r\n').encode('utf-8-sig'))
        mt('mcopy', '-i', part, readme, '::/Icons/README.txt')
        blank = os.path.join(tmp, 'NEWTAPE.cas')                      # 빈 테이프 (복사해서 새 테이프로)
        open(blank, 'wb').close()
        mt('mcopy', '-i', part, blank, '::/Storage Device/CAS/NEWTAPE.cas')
        ini = os.path.join(tmp, 'x86msx.ini')
        text = '; X86MSX settings and last state (written automatically)\n[State]\nMachine=MSX - C-BIOS\nLanguage=ko\nVolume=70\nMute=0\nResetAfterInsert=1\nHdResetEject=0\nHdResetInsert=1\nVhdResetInsert=1\nDiskResetAfterInsert=0\nDiskAccel=1\nFlashAuto=3\nGlobalSave=1\nResume=1\nBootCtrl=0\nMonitor=0\nMonitorEmu2=0\nFxBlur=0\nFxGlow=0\nFxNoise=0\nAspect=0\nScanlines=0\nBright=0\nContrast=100\nSatur=100\nGamma=100\nVdpCmdTiming=1\nSpriteHide=0\nSpriteNoLimit=0\nHideIcons=0\nIconsAlways=0\nShotFull=1\nPowerSave=2\nJoy1=0\nJoy2=0\nMousePort=0\nMouseSens=100\nMouseMenu=1\nMouseUse=1\nMouseInvX=0\nMouseInvY=0\nMouseLock=0\nMousePointer=0\nMouseWheel=1\nMouseDriver=0\nMouseOff=0\nKeyLayout=0\nPrnMode=0\nPrnPreview=1\nStereo=1\nMidiInternal=1\nMidiIn=0\nMidiInPort=0\nMt32Model=0\nMidiRev=100\nMidiCho=100\nMidiLen=100\nMidiFxOn=1\nSndSync=1\nSc55Set=0\nDriveSound=0\nMidiOutMode=1\nMidiSynth=2\nPanaFront=0\nPanaPause=0\nMute=0\nMixPSG=100\nMutePSG=0\nBalancePSG=0\nMixPSG2=100\nMutePSG2=0\nBalancePSG2=0\nMixSCC=100\nMuteSCC=0\nBalanceSCC=0\nMixM-M=100\nMuteM-M=0\nBalanceM-M=0\nMixM-A=100\nMuteM-A=0\nBalanceM-A=0\nMixPCM=100\nMutePCM=0\nBalancePCM=0\nMixM-S=100\nMuteM-S=0\nBalanceM-S=0\nMixOPP=100\nMuteOPP=0\nBalanceOPP=0\nMixMIDI=100\nMuteMIDI=0\nBalanceMIDI=0\nMixKBD=60\nMuteKBD=0\nMixI-O=60\nBalanceI-O=0\nMixMIDIINT=100\nMuteMIDIINT=0\n'   # 8.0.39: 필요한 설정만 (경로·최근 파일·네트워크·기기별 값은 프로그램이 처음 쓸 때 채움)
        data = text.encode() + b' ' * (16384 - 1 - len(text.encode())) + b'\n'   # 본체 INI_SIZE와 같은 16 KiB (3.0)
        open(ini, 'wb').write(data)
        mt('mcopy', '-i', part, ini, '::/x86msx.ini')
        rd = os.path.join(tmp, 'readme.txt')
        with open(rd, 'wb') as f:
            f.write(README.replace('\n', '\r\n').encode('utf-8-sig'))
        mt('mcopy', '-i', part, rd, '::/README.txt')
        listing = subprocess.run(['mdir', '-i', part, '-/', '::/'], env=env, capture_output=True, text=True).stdout
        part_data = open(part, 'rb').read()

    img = bytes(head) + b'\0' * (PART_LBA * 512 - len(head)) + part_data
    assert len(img) == total * 512
    open(out, 'wb').write(img)
    print('%s: %d MiB, loader %d sectors, kernel %d bytes, FAT32 partition at LBA %d (%d MiB)'
          % (out, IMAGE_MIB, len(loader) // 512, len(kernel), PART_LBA, part_sectors // 2048))
    for line in listing.splitlines():
        if line.strip() and ('ROM' in line or 'INI' in line or 'TXT' in line or 'BIN' in line or 'Directory' in line or '<DIR>' in line):
            print('  ' + line.strip())


main()
