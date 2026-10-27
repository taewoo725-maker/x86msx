#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# 3.2.0 zip 롬팩: 압축(deflate)·저장, 폴더 안, 다른 파일 먼저인 zip에서 롬을 꺼내는지
set -e
d=build/zipt; mkdir -p $d
python3 - <<'PY'
import zipfile, os, random
d='build/zipt'; random.seed(1)
rom=bytes(random.randrange(256) for _ in range(131072)); open(d+'/a.rom','wb').write(rom)
with zipfile.ZipFile(d+'/defl.zip','w',zipfile.ZIP_DEFLATED) as z: z.writestr('readme.txt','hello'*100); z.writestr('GAME/Some Game.ROM', rom)
with zipfile.ZipFile(d+'/store.zip','w',zipfile.ZIP_STORED) as z: z.writestr('game.mx2', rom)
PY
n=0
./build/ziptest $d/defl.zip $d/a.rom "rom|mx1|mx2" && n=$((n+1))
./build/ziptest $d/store.zip $d/a.rom "rom|mx1|mx2" && n=$((n+1))
echo "ziptest: $n/2 passed"
