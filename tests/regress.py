#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""regress.py - 회귀 시험 묶음 (구조 개편 0단계, 5.0.185)

tests/regress/cases.txt 의 시험마다 build/msxrun 을 돌려 정한 프레임의 화면(PPM)과 소리(WAV)의
SHA-1 을 tests/regress/expected.txt 와 비교한다. 속도(초당 프레임)도 재서 perf.txt 기준보다
전체 평균이 20% 넘게 느려지면 경고한다.

  python3 tests/regress.py            비교 (하나라도 다르면 종료 코드 1)
  python3 tests/regress.py --update   지금 결과를 기준으로 저장 (동작을 일부러 바꿨을 때만)
  python3 tests/regress.py --twice    같은 시험을 두 번 돌려 결과가 같은지 (결정성) 확인
  python3 tests/regress.py 이름...     몇 개만
다른 화면은 build/regress/<이름>_<프레임>.png 로 남긴다 (기준 화면은 _ref.png 가 아니라 저장소에 없음 — 해시만 보관).
"""
import hashlib, os, shlex, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RD = os.path.join(ROOT, 'tests', 'regress')
OUT = os.path.join(ROOT, 'build', 'regress')
MSXRUN = os.path.join(ROOT, 'build', 'msxrun')
FIXED_ENV = {'RTC_NOW': '2026-01-01 00:00:00'}   # 시계 고정 (결정성)

def load_cases():
    cases = []
    for ln in open(os.path.join(RD, 'cases.txt'), encoding='utf-8'):
        ln = ln.rstrip('\n')
        if not ln.strip() or ln.lstrip().startswith('#'): continue
        f = [x.strip() for x in ln.split('|')] + [''] * 7
        cases.append(dict(name=f[0], prof=f[1], frames=int(f[2]), shots=[int(x) for x in f[3].split(',') if x],
                          env=dict(kv.split('=', 1) for kv in f[4].split()), cart=f[5], args=shlex.split(f[6])))
    return cases

def missing(c):
    p = os.path.join(ROOT, c['prof'], 'config.ini')
    if not os.path.isfile(p): return 'no profile'
    if c['cart'] and not os.path.isfile(os.path.join(ROOT, c['cart'])): return 'no cart ' + c['cart']
    for ln in open(p, encoding='utf-8', errors='replace'):
        if '"' in ln and ln.split()[0:1] and ln.split()[0].isdigit():
            q = ln.split('"')
            if len(q) > 1 and q[1] and not os.path.isfile(os.path.join(ROOT, c['prof'], os.path.basename(q[1].replace('\\', '/')))):
                return 'no rom ' + os.path.basename(q[1])
    return None

def run(c, tag=''):
    os.makedirs(OUT, exist_ok=True)
    shots = [os.path.join(OUT, f"{c['name']}{tag}_{f}.ppm") for f in c['shots']]
    wav = os.path.join(OUT, f"{c['name']}{tag}.wav")
    cmd = [MSXRUN, os.path.join(ROOT, c['prof']), str(c['frames'])]
    for f, p in zip(c['shots'], shots): cmd += ['--shot', str(f), p]
    cmd += c['args'] + ['--wav', wav]
    env = dict(os.environ, **FIXED_ENV, **{k: v.replace('$ROOT', ROOT) for k, v in c['env'].items()})
    if c['cart']: env['CART'] = os.path.join(ROOT, c['cart'])
    t0 = time.time()
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    dt = time.time() - t0
    if r.returncode: return None, dt, r.stderr.strip() or r.stdout.strip()
    res = {}
    for f, p in zip(c['shots'], shots):
        res[f'shot{f}'] = hashlib.sha1(open(p, 'rb').read()).hexdigest() if os.path.isfile(p) else 'none'
    res['wav'] = hashlib.sha1(open(wav, 'rb').read()).hexdigest() if os.path.isfile(wav) else 'none'
    return res, dt, ''

def load_kv(fn):
    d = {}
    p = os.path.join(RD, fn)
    if os.path.isfile(p):
        for ln in open(p, encoding='utf-8'):
            k = ln.split()
            if len(k) >= 3 and not ln.startswith('#'): d[(k[0], k[1])] = k[2]
    return d

def to_png(ppm):
    try:
        from PIL import Image
        Image.open(ppm).save(ppm[:-4] + '.png')
    except Exception:
        pass

def main():
    a = sys.argv[1:]
    update, twice = '--update' in a, '--twice' in a
    only = [x for x in a if not x.startswith('--')]
    if not os.path.isfile(MSXRUN):
        print('build/msxrun 이 없음: make build/msxrun'); return 2
    exp, perf = load_kv('expected.txt'), load_kv('perf.txt')
    new_exp, new_perf = dict(exp), dict(perf)
    npass = nfail = nskip = 0
    tot_frames, tot_time = 0, 0.0
    for c in load_cases():
        if only and c['name'] not in only: continue
        why = missing(c)
        if why:
            print(f"SKIP {c['name']:<14} ({why})"); nskip += 1; continue
        res, dt, err = run(c)
        if res is None:
            print(f"FAIL {c['name']:<14} 실행 오류: {err[:200]}"); nfail += 1; continue
        fps = c['frames'] / dt if dt > 0 else 0
        tot_frames += c['frames']; tot_time += dt
        bad = []
        if twice:
            res2, _, _ = run(c, '_b')
            if res2 != res: bad.append('결정성: 두 번 결과가 다름 ' + ','.join(k for k in res if res2 and res2.get(k) != res[k]))
        for k, v in res.items():
            if update: new_exp[(c['name'], k)] = v
            elif (c['name'], k) not in exp: bad.append(f'{k}: 기준 없음 (--update 필요)')
            elif exp[(c['name'], k)] != v:
                bad.append(f'{k}: 다름')
                if k.startswith('shot'): to_png(os.path.join(OUT, f"{c['name']}_{k[4:]}.ppm"))
        slow = ''   # 짧은 시험 하나의 fps 는 흔들리므로 경고는 전체 평균으로만
        if bad:
            nfail += 1; print(f"FAIL {c['name']:<14} {fps:6.0f} fps  " + '; '.join(bad) + slow)
        else:
            npass += 1; print(f"PASS {c['name']:<14} {fps:6.0f} fps{slow}")
    avg = tot_frames / tot_time if tot_time else 0
    ref = perf.get(('ALL', 'fps'))
    if not only and ref and avg < float(ref) * 0.8: print(f'※ 전체 속도가 기준({float(ref):.0f} fps)보다 20% 넘게 느려짐')
    if update and not only: new_perf[('ALL', 'fps')] = f'{avg:.0f}'
    if update:
        with open(os.path.join(RD, 'expected.txt'), 'w', encoding='utf-8') as f:
            f.write('# 이름 항목 SHA-1  (python3 tests/regress.py --update 로 만듦 — 동작을 일부러 바꿨을 때만 갱신)\n')
            for (n, k), v in sorted(new_exp.items()): f.write(f'{n} {k} {v}\n')
        with open(os.path.join(RD, 'perf.txt'), 'w', encoding='utf-8') as f:
            f.write('# 이름 fps 값  (호스트 기준 속도, 20% 넘게 느려지면 경고)\n')
            for (n, k), v in sorted(new_perf.items()): f.write(f'{n} {k} {v}\n')
    print(f'regress: {npass} pass, {nfail} fail, {nskip} skip, 평균 {avg:.0f} fps' + (' (기준 저장함)' if update else ''))
    return 1 if nfail else 0

if __name__ == '__main__':
    sys.exit(main())
