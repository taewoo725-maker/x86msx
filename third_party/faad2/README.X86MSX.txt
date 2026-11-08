faad2 (https://github.com/knik0/faad2, 커밋 a335040) - AAC 해독기, GNU GPL v2 이상 (COPYING). X86MSX 는 GPL-3.0-or-later 라 합쳐 쓸 수 있음.
Freeware Advanced Audio (AAC) Decoder including SBR decoding, Copyright (C) 2003-2005 M. Bakker, Nero AG, http://www.nero.com
X86MSX 에서 바꾼 것 (변경 고지):
  - faad_cfg.h 새로 만듦 (AAC-LC 만: LC_ONLY_DECODER + DISABLE_SBR, 부동소수점, NDEBUG), common.h 맨 앞에서 불러옴.
  - common.c: faad_malloc/faad_free 를 src/mp4play.c 의 고정 영역 할당기(mp4h_alloc/mp4h_free)로, pow 대신 faad_pow(밑 2 또는 0.5 전용) 추가.
  - 쓰지 않는 파일(SBR·PS·DRM·SSR·frontend)은 가져오지 않음.
쓰는 곳: src/mp4play.c (X86 Multimedia 영상의 MP4 소리). 8.0.97: SBR·PS(HE-AAC, HE-AACv2) 해독 추가 (sbr_*.c, ps_*.c 를 같은 커밋에서 가져옴; pow·log·sqrt·qsort 는 faad_* 대용품, 본체 판에서 abs·fprintf·stderr 는 faad_cfg.h 에서 대체). DISABLE_SBR 는 뺌.
