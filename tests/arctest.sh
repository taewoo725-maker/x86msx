#!/bin/sh
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# 8.0.44 .ARJ·.PMA 읽기 시험. PMA 는 MSX 의 PMARC·PMARC2 로 만든 진짜 파일(lhasa 시험 자료),
# ARJ 는 tests/arjenc.py 가 만든 방법 0·1·4 파일 + 진짜 bla.arj (commons-compress 시험 자료).
set -e
cd "$(dirname "$0")/.."
make -s build/arctest >/dev/null
rm -rf build/arjt; python3 tests/arjenc.py build/arjt >/dev/null
./build/arctest tests/data/arc/*.pma tests/data/arc/*.lz[hs] tests/data/arc/bla.arj build/arjt/m0.arj build/arjt/m1.arj build/arjt/m4.arj > build/arctest.log 2>&1 || { tail -20 build/arctest.log; echo "arctest: FAIL"; exit 1; }
# 뒤틀린 파일로 죽지 않는지 (ASAN·UBSAN)
python3 - <<'PY'
import random, subprocess, sys, glob
random.seed(5)
srcs = glob.glob("tests/data/arc/*.pma") + glob.glob("tests/data/arc/*.lz[hs]") + ['build/arjt/m1.arj', 'build/arjt/m4.arj']
for it in range(80):
    d = bytearray(open(random.choice(srcs), 'rb').read())
    for _ in range(random.choice([1, 3, 10])): d[random.randrange(len(d))] = random.randrange(256)
    if random.random() < 0.3: d = d[:random.randrange(len(d))]
    open('build/arcfz.bin', 'wb').write(d)
    r = subprocess.run(['./build/arctest', 'build/arcfz.bin'], capture_output=True, env={'ASAN_OPTIONS': 'detect_leaks=0'})
    e = r.stderr.decode('latin1')
    if 'Sanitizer' in e or 'runtime error' in e or r.returncode < 0:
        print(e[:500]); sys.exit(1)
PY
echo "arctest: ok (PMA pm0/pm1/pm2, LZH lh1/4/5/6/7/lz5/lzs, ARJ 0/1/4, fuzz)"
