h264bsd (https://github.com/oneam/h264bsd) - H.264 Baseline 해독기, Apache License 2.0 (LICENSE.md).
원본은 Android Open Source Project 에서 뽑은 것 (Copyright (C) 2009 The Android Open Source Project).
X86MSX 에서 바꾼 것 (Apache-2.0 4(b) 변경 고지):
  - h264bsd_cfg.h: <stdlib.h> 대신 mp4h_alloc/mp4h_free (src/mp4play.c 의 고정 영역 할당기)로 malloc/free 를 바꿈.
나머지 파일은 원본 그대로. 쓰는 곳: src/mp4play.c (X86 Multimedia 영상의 MP4 재생).
알려진 제한: Baseline 만 (CAVLC, B프레임·가중 예측 없음). UBSan 은 deblocking 의 음수 왼쪽 이동을 지적하나 x86 에서 산술 이동으로 동작함.
