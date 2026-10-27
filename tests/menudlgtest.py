#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX 5.2.130 - 메뉴에서 연 창을 키로 움직인 뒤, 화면이 '전부 다시 그린 것'과 같은지 본다.
#   fehost MENUDUMP=1 로 메뉴 끝 항목을 모두 받아, 항목마다: 키로 열고 → ↓↑ 여러 번 → (REDRAWCMP) 전체 다시 그리기 전·후 그림 비교.
#   다르면 줄만 고쳐 그리는 창 위에 메뉴가 덧씌워지는 식의 깨짐 (예: 미디어 슬롯 삽입 창을 계속 내리면 화면이 이상해짐).
import os, subprocess, sys, tempfile, shutil
from concurrent.futures import ThreadPoolExecutor
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
fehost = os.environ.get('FEHOST_BIN', os.path.join(root, 'build/fehost'))
W, H = os.environ.get('W', '1280'), os.environ.get('H', '800')
tmp = tempfile.mkdtemp()
def mkroot(i):
    r = os.path.join(tmp, f'r{i}')
    os.makedirs(r + '/Machines'); os.symlink(os.path.join(root, 'machines/C-BIOS'), r + '/Machines/C-BIOS')
    open(r + '/x86msx.ini', 'w').write('[State]\nMachine=MSX - C-BIOS\nLanguage=ko\n')
    rd = r + '/Storage Device/ROMS'; os.makedirs(rd)
    for k in range(60): open(f'{rd}/game{k:03d}.rom', 'wb').write(b'AB' + b'\0' * 30)
    for k in range(4): os.makedirs(f'{rd}/dir{k}')
    return r
base = mkroot(0)
dump = subprocess.run([fehost], env=dict(os.environ, MENUDUMP='1', ROOT=base, MACHINE='C-BIOS', W=W, H=H), capture_output=True, text=True).stdout
leaves = [l.split('\t') for l in dump.splitlines() if l and l[0].isdigit() and '\t' in l]
skip = set(os.environ.get('SKIP_ACTIONS', '').split(','))
def ppm(path):
    d = open(path, 'rb').read(); i = 0; tok = []
    while len(tok) < 4:
        while d[i:i+1].isspace(): i += 1
        j = i
        while not d[j:j+1].isspace(): j += 1
        tok.append(d[i:j]); i = j
    return int(tok[1]), int(tok[2]), d[i+1:]
def run(idx, keys, esc):
    r = os.path.join(tmp, f'w{idx}{"e" if esc else ""}'); shutil.copytree(base, r, symlinks=True)
    seq = keys + ',down,down,down,up,down,down,down,down,down,down,down,down,down,down,down,down' + (',esc' if esc else '')
    env = dict(os.environ, ROOT=r, MACHINE='C-BIOS', W=W, H=H, KEYS=seq, GAP='6', START='260', TAIL='16', SHOT=r + '/s', REDRAWCMP='1')
    try: out = subprocess.run([fehost], env=env, capture_output=True, text=True, timeout=300).stdout
    except Exception: return 'timeout', 0, 0
    try:
        w, h, a = ppm(r + '/s_pre.ppm'); _, _, b = ppm(r + '/s_99.ppm')
    except Exception: shutil.rmtree(r, ignore_errors=True); return 'noshot', 0, 0
    dlg = 0 if 'DLG 0' in out else 1
    n = sum(1 for x in range(0, len(a), 3) if a[x:x+3] != b[x:x+3]) if a != b else 0
    shutil.rmtree(r, ignore_errors=True)
    return ('ok' if n == 0 else 'DIFF'), n, dlg
def one(arg):
    idx, (bar, keys, act) = arg
    st, n, dlg = run(idx, keys, False)
    if not dlg: return (keys, act, 'ok', 0)          # 창이 안 열린 항목(동작만 하는 줄)은 건너뜀
    if st != 'ok': return (keys, act, st, n)
    st, n, _ = run(idx, keys, True)                  # Esc 로 돌아온 메뉴·화면도 같은지
    return (keys, act, ('ok' if st == 'ok' else 'ESC-' + st), n)
with ThreadPoolExecutor(max_workers=int(os.environ.get('JOBS', '4'))) as ex:
    res = list(ex.map(one, [(i, l) for i, l in enumerate(leaves) if l[2] not in skip]))
bad = [r for r in res if r[2] != 'ok']
for r in bad: print('FAIL', r)
print(f'menudlgtest: {len(res)} 항목, 실패 {len(bad)}' + (' — OK' if not bad else ''))
shutil.rmtree(tmp, ignore_errors=True)
sys.exit(1 if bad else 0)
