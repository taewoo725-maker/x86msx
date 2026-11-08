#!/bin/bash
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# 8.0.120 AVI(MJPEG+PCM) 시험: ffmpeg 로 파일을 만들고 build/avitest 로 확인
set -e
D=$(mktemp -d); trap 'rm -rf $D' EXIT
F="ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=15:duration=2 -f lavfi -i sine=frequency=440:duration=2"
$F -c:v mjpeg -pix_fmt yuvj420p -c:a pcm_s16le $D/a.avi
$F -c:v mjpeg -pix_fmt yuvj444p -c:a pcm_s16le $D/b.avi
./build/avitest avi $D/a.avi 30 2 | tail -1
./build/avitest avi $D/b.avi 30 2 | tail -1
# 8.0.153 MOV 의 PCM 소리 5종: ffmpeg 가 푼 값과 같은지
for c in pcm_s16le pcm_s16be pcm_u8 pcm_s24be pcm_s32be; do
  ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=15:duration=3,format=yuv420p -f lavfi -i sine=frequency=440:sample_rate=44100:duration=3 -ac 2 -c:v libx264 -profile:v baseline -c:a $c $D/p_$c.mov
  ffmpeg -loglevel error -y -i $D/p_$c.mov -vn -f s16le -acodec pcm_s16le $D/p_$c.raw
  echo -n "$c: "; ./build/avitest pcmcmp $D/p_$c.mov $D/p_$c.raw | tail -1
done
# 8.0.153 ALAC (Apple Lossless): MOV·MP4 안의 16비트 / 24비트, ffmpeg 가 푼 값과 같은지
for spec in "mov alac s16p" "mp4 alac s16p" "mov alac s32p"; do
  set -- $spec; ext=$1; sf=$3
  ffmpeg -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=15:duration=3,format=yuv420p -f lavfi -i sine=frequency=440:sample_rate=44100:duration=3 -ac 2 -c:v libx264 -profile:v baseline -c:a alac -sample_fmt $sf $D/al_$sf.$ext
  ffmpeg -loglevel error -y -i $D/al_$sf.$ext -vn -f s16le -acodec pcm_s16le $D/al_$sf.raw
  echo -n "alac $ext $sf: "; ./build/avitest pcmcmp $D/al_$sf.$ext $D/al_$sf.raw | tail -1
done
