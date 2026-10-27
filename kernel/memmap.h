/* 만든이: KimTaeWoo */
/* 도와준이: Lee.J.H */
/* X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE) */
/* memmap.h - 레거시(BIOS) 본체의 고정 메모리 배치 (3.0, 설계서 3.x §1.1)
 *
 *   0x00100000 ~ 0x033FFFFF  본체 이미지 + BSS (링크 뒤 Makefile이 __bss_end < MEM_KERNEL_END 검사)
 *                            3.4.4: 카트리지 버퍼가 슬롯마다 2 MiB가 되어 4 MiB 늘림 (뒤 영역을 모두 4 MiB 뒤로)
 *                            3.4.7: 긴 파일 이름(이름 768바이트·경로 1 KiB)으로 탐색 목록이 커져 다시 4 MiB 늘림
 *                            3.6.7: 카트리지 5칸 (버퍼 2 MiB 더)으로 다시 4 MiB 늘림
 *                            4.0.29: V9990과 BSS 증가로 다시 4 MiB 늘림
 *                            부드러운 글꼴(Noto) 데이터로 다시 4 MiB 늘림
 *                            장치별 상태 저장·진단 LOG 로 BSS 여유가 없어 다시 4 MiB 늘림 (뒤 영역을 모두 4 MiB 뒤로)
 *   0x03800000 ~ 0x03BFFFFF  HD Audio DMA (4 MiB 페이지 하나를 캐시 안 됨으로)
 *   0x03C00000 ~ 0x03FFFFFF  외장 매퍼 RAM xram 4 MiB
 *   0x04000000 ~ 0x05FFFFFF  작업 메모리 scratch 32 MiB (상태·PNG·녹음)
 *   0x06000000 ~ 0x063FFFFF  MoonSound(OPL4) 웨이브 메모리 4 MiB (롬 2 + 램 2, 3.1.0)
 *   0x06400000 ~ 0x07BFFFFF  3.7.1 가상 하드 (폴더 → FAT 볼륨 6개 + 쓴 섹터 덮개) 24 MiB
 *   0x07C00000 ~ 0x07DFFFFF  4.0.29 V9990(Gfx9000) VRAM·레지스터·화면 버퍼 2 MiB
 *   0x07E00000 ~ 0x08DFFFFF  5.0.75 화면 뒤 버퍼 16 MiB (BIOS 부팅: 한 프레임을 여기 그린 뒤 바뀐 줄만 화면으로)
 *   0x08E00000 ~ 0x0DDFFFFF  USB 전체 용량 사용 때 파일 임시 보관 80 MiB (scratch와 겹치지 않음)
 * 실행에 필요한 메모리 끝 = MEM_RUN_END (142 MiB), USB 확장까지 = MEM_GROW_END (222 MiB). */
#ifndef X86MSX_MEMMAP_H
#define X86MSX_MEMMAP_H
#define MEM_KERNEL_END  0x03800000u
#define MEM_HDA_DMA     0x03800000u
#define MEM_XRAM        0x03C00000u
#define MEM_XRAM_SIZE   (4u << 20)
#define MEM_SCRATCH     0x04000000u
#define MEM_SCRATCH_SIZE (32u << 20)
#define MEM_WAVE        0x06000000u
#define MEM_WAVE_SIZE   (4u << 20)
#define MEM_VHD         0x06400000u
#define MEM_VHD_SIZE    (24u << 20)
#define MEM_V9990       0x07C00000u
#define MEM_V9990_SIZE  (2u << 20)
#define MEM_BACK        0x07E00000u
#define MEM_BACK_SIZE   (16u << 20)
#define MEM_GROW        0x08E00000u
#define MEM_GROW_SIZE   (80u << 20)
#define MEM_RUN_END     MEM_GROW
#define MEM_GROW_END    (MEM_GROW + MEM_GROW_SIZE)
#endif
