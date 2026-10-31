#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# 마스터님 그림(MSX_그림 폴더)을 시스템 보기용 투명 PNG 로 만든다.  usage: make_sysview_art.py <그림 폴더> <내보낼 폴더>
import sys, os
import numpy as np, cv2
from scipy import ndimage as ndi
src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)

def cutout(name, thr=3, dark=0):
    """어두운 푸른 배경(+바닥 빛 타원)을 바깥에서부터 지워 투명하게, 그림자를 새로 깔아 줌"""
    im = cv2.imread(os.path.join(src, name), cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.int32)   # RGB
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    lum = (r * 3 + g * 6 + b) // 10
    bgish = ((b >= r + thr) & (lum < 125)) | (lum < dark)
    lab, n = ndi.label(bgish)
    edge = np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))
    bg = np.isin(lab, edge[edge > 0])
    fg = ~bg
    fg = ndi.binary_opening(fg, iterations=2)                              # 먼지 제거
    lab, n = ndi.label(fg); sizes = ndi.sum(fg, lab, range(1, n + 1))
    fg = lab == (1 + int(np.argmax(sizes)))                                # 가장 큰 덩이만
    fg = ndi.binary_fill_holes(fg)
    a = ndi.gaussian_filter(ndi.binary_erosion(fg, iterations=1).astype(np.float32), 1.0)
    rgb = im.astype(np.float32)
    return rgb, a

def add_shadow(rgb, a, depth=0.05, blur=0.025, op=0.32):
    h, w = a.shape
    ys, xs = np.where(a > 0.5); y1 = ys.max(); x0, x1 = xs.min(), xs.max()
    sh = np.zeros_like(a)
    d = int(h * depth)
    band = a[max(0, y1 - int(h * 0.04)):y1 + 1].max(axis=0)                # 바닥 둘레만 그림자로
    sh[y1 - d // 2:y1 + d, :] = band[None, :] * 1.0
    sh = ndi.gaussian_filter(sh, blur * w) * op
    return sh

def finish(name, rgb, a, shadow, width, extra=None):
    h, w = a.shape
    pad = int(w * 0.03)
    sh = shadow if shadow is not None else np.zeros_like(a)
    tot = np.maximum(a, sh)
    ys, xs = np.where(tot > 0.02)
    y0, y1, x0, x1 = max(0, ys.min() - pad), min(h, ys.max() + pad), max(0, xs.min() - pad), min(w, xs.max() + pad)
    # 색: 물체 + 그림자(검정) 를 알파 합성
    A = np.clip(a + sh * (1 - a), 0, 1)
    C = np.where(A[..., None] > 1e-4, (rgb * a[..., None]) / np.maximum(A[..., None], 1e-4), 0)
    rgba = np.dstack([C, A * 255]).astype(np.float32)[y0:y1, x0:x1]
    sc = width / rgba.shape[1]
    nh = max(1, int(rgba.shape[0] * sc))
    # 알파 곱한 값으로 줄여 가장자리 번짐 방지
    pm = rgba.copy(); pm[..., :3] *= pm[..., 3:4] / 255.0
    pm = cv2.resize(pm, (width, nh), interpolation=cv2.INTER_AREA)
    al = pm[..., 3:4]
    col = np.where(al > 0.5, pm[..., :3] / np.maximum(al / 255.0, 1e-4), 0)
    res = np.dstack([np.clip(col, 0, 255), np.clip(al, 0, 255)]).astype(np.uint8)
    cv2.imwrite(os.path.join(out, name), res[..., [2, 1, 0, 3]], [cv2.IMWRITE_PNG_COMPRESSION, 9])
    by, bx = np.where(res[..., 3] > 240)                                   # 몸체(그림자 뺀) 범위 — 쌓기·글자 자리용
    open(os.path.join(out, name + ".txt"), "w").write("body=%d,%d,%d,%d\n" % (bx.min(), by.min(), bx.max() - bx.min() + 1, by.max() - by.min() + 1))
    return (x0, y0, sc), res.shape

for f, o, wd in (("f10_diskdrive.png", "drive.png", 720), ("f10_floppy.png", "drivefd.png", 720), ("f10_harddisk.png", "hd.png", 720),
                 ("f10_midi.png", "midi.png", 860), ("f10_gm.png", "gm.png", 860), ("msx_joystick.png", "joystick.png", 460)):
    rgb, a = cutout(f)
    sh = add_shadow(rgb, a) if f != "msx_joystick.png" else add_shadow(rgb, a, 0.03, 0.02, 0.28)
    m, shp = finish(o, rgb, a, sh, wd)
    print(o, shp, m)

# ---- 모니터: 화면 자리를 투명하게 (그 자리에 실제 MSX 화면을 그림), 살짝 유리 반사 ----
def monitor():
    im = cv2.imread(os.path.join(src, "msx_monitor_transparent.png"), cv2.IMREAD_UNCHANGED).astype(np.float32)   # BGRA
    h, w = im.shape[:2]
    X0, X1, Y0, Y1 = 516, 1883, 396, 1418                                  # 파란 화면 상자 (원본 픽셀, 둥근 모서리 포함)
    rr = 96
    yy, xx = np.mgrid[0:h, 0:w]
    cx0, cx1, cy0, cy1 = X0 + rr, X1 - rr, Y0 + rr, Y1 - rr
    dx = np.maximum(np.maximum(cx0 - xx, xx - cx1), 0); dy = np.maximum(np.maximum(cy0 - yy, yy - cy1), 0)
    dist = np.sqrt(dx * dx + dy * dy)
    hole = np.clip((rr - 8 - dist) / 2.5 + 0.5, 0, 1)                      # 화면 안쪽 = 1
    hole *= ((xx >= X0) & (xx <= X1) & (yy >= Y0) & (yy <= Y1))
    # 유리 반사: 왼쪽 위에서 오른쪽 아래로 옅은 흰 빛 + 가장자리 어둡게
    u = (xx - X0) / (X1 - X0); v = (yy - Y0) / (Y1 - Y0)
    gloss = 0.13 * np.clip(1 - (u * 0.8 + v * 0.8) * 1.3, 0, 1) ** 1.6
    vig = np.clip((np.maximum(np.abs(u - .5), np.abs(v - .5)) - 0.38) * 1.2, 0, 0.35)
    ga = np.clip(gloss + vig, 0, 1)
    gcol = np.where(vig > gloss, 0.0, 255.0)                  # 가장자리는 검정, 반사는 흰색
    a0 = im[..., 3] / 255.0
    na = np.where(hole > 0, ga * hole, a0)                                 # 화면 안: 반사·그늘만 반투명
    col = im[..., :3].copy()
    for c in range(3): col[..., c] = np.where(hole > 0, gcol, col[..., c])
    res = np.dstack([col, na * 255])
    ys, xs = np.where(a0 > 0.02); pad = 20
    y0, y1, x0, x1 = ys.min() - pad, ys.max() + pad, xs.min() - pad, xs.max() + pad
    res = res[max(0, y0):y1, max(0, x0):x1]
    wd = 960; sc = wd / res.shape[1]; nh = int(res.shape[0] * sc)
    pm = res.copy(); pm[..., :3] *= pm[..., 3:4] / 255.0
    pm = cv2.resize(pm, (wd, nh), interpolation=cv2.INTER_AREA)
    al = pm[..., 3:4]; colr = np.where(al > 0.5, pm[..., :3] / np.maximum(al / 255.0, 1e-4), 0)
    fin = np.dstack([np.clip(colr, 0, 255), np.clip(al, 0, 255)]).astype(np.uint8)
    cv2.imwrite(os.path.join(out, "monitor.png"), fin, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    sx, sy = (X0 - max(0, x0)) * sc, (Y0 - max(0, y0)) * sc; sw, sh = (X1 - X0) * sc, (Y1 - Y0) * sc
    open(os.path.join(out, "monitor.png.txt"), "w").write("screen=%d,%d,%d,%d\nradius=%d\n" % (round(sx), round(sy), round(sw), round(sh), round(rr * sc)))
    print("monitor", fin.shape, round(sx), round(sy), round(sw), round(sh))

# ---- 본체(기본): 꽂혀 있던 롬 카트리지를 지우고 윗면을 이어 붙임 ----
def computer():
    c = cv2.imread(os.path.join(src, "msx_computer_transparent.png"), cv2.IMREAD_UNCHANGED)
    TOP = 1001                                                             # 본체 윗면 시작 줄
    cx0, cx1, cy1 = 2080, 2430, 1040                                       # 카트리지가 가린 자리
    patch = c[TOP:cy1, 1650 - 0:1650 + (cx1 - cx0)].copy()                 # 첫 슬롯 둘레를 옮겨 둘째 슬롯 자리를 채움
    c[780:TOP, cx0 - 20:cx1 + 20, 3] = 0                                   # 윗면 위쪽(카트리지 몸) 투명
    c[TOP:cy1, cx0:cx1] = patch
    # 카트리지 오른쪽 가장자리 잔상
    c[780:TOP, :, 3] = 0
    ys, xs = np.where(c[..., 3] > 10); y0, y1, x0, x1 = TOP - 40, ys.max() + 30, xs.min() - 10, xs.max() + 10
    c = c[y0:y1, max(0, x0):x1]
    wd = 1500; sc = wd / c.shape[1]; nh = int(c.shape[0] * sc)
    pm = c.astype(np.float32); pm[..., :3] *= pm[..., 3:4] / 255.0
    pm = cv2.resize(pm, (wd, nh), interpolation=cv2.INTER_AREA)
    al = pm[..., 3:4]; colr = np.where(al > 0.5, pm[..., :3] / np.maximum(al / 255.0, 1e-4), 0)
    fin = np.dstack([np.clip(colr, 0, 255), np.clip(al, 0, 255)]).astype(np.uint8)
    cv2.imwrite(os.path.join(out, "default.png"), fin, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    s1 = round((1650 + 190 - max(0, x0)) * sc); s2 = round((1650 + 190 + 430 - max(0, x0)) * sc); ty = round((TOP - y0) * sc)
    open(os.path.join(out, "default.png.txt"), "w").write("slot1=%d,%d\nslot2=%d,%d\n" % (s1 - 95, ty - 85, s2 - 95, ty - 85))
    print("default", fin.shape)


# ---- 4슬롯 확장 박스 + 롬 카트리지 (extra 폴더: expander.png 빈 박스, expander_rom.png 롬 꽂힌 박스) ----
def expander_cart(extra):
    global src
    save = src; src = extra
    rgb, a = cutout("expander.png", 9, 22)
    sh = add_shadow(rgb, a, 0.04, 0.025, 0.30)
    m, shp = finish("expander.png", rgb, a, sh, 760)
    (x0, y0, sc) = m
    slots = [(951, 806, 985), (955, 640, 910), (960, 497, 850), (962, 368, 795)]   # 슬롯 0(아래)~3: 가운데 x, 윗줄 y, 폭
    lines = []
    for cx, top, wd in slots:
        k = wd / 795.0; cw = 580 * k; bot = top + 64 * k
        lines.append("%d,%d,%d,0" % (round((cx - x0) * sc), round((bot - y0) * sc), round(cw * sc)))
    open(os.path.join(out, "expander.png.txt"), "a").write("".join("slot%d=%s\n" % (i, l) for i, l in enumerate(lines)))
    print("expander", shp, lines)
    # 카트리지: 롬 꽂힌 그림에서 떼어냄 (다각형 모양)
    im = cv2.imread(os.path.join(src, "expander_rom.png"), cv2.IMREAD_COLOR)
    X0, Y0, X1, Y1 = 668, 172, 1254, 436
    crop = im[Y0:Y1, X0:X1]
    mask = np.zeros(crop.shape[:2], np.uint8)
    poly = np.array([(670, 176), (1250, 176), (1251, 202), (1245, 433), (680, 433), (670, 202)]) - [X0, Y0]
    cv2.fillPoly(mask, [poly.astype(np.int32)], 255)
    mask = cv2.GaussianBlur(cv2.erode(mask, np.ones((3, 3), np.uint8)), (3, 3), 0.8)
    rgba = np.dstack([crop, mask])
    cv2.imwrite(os.path.join(out, "cart.png"), rgba, [cv2.IMWRITE_PNG_COMPRESSION, 9])
    open(os.path.join(out, "cart.png.txt"), "w").write("label=%d,%d,%d,%d\n" % (705 - X0, 289 - Y0, 510, 121))
    print("cart", rgba.shape)
    src = save

expander_cart(os.path.join(os.path.dirname(os.path.abspath(src)), "extra"))
monitor(); computer()

# ---- 마우스 (extra/mouse.png) ----
def mouse(extra):
    global src
    save = src; src = extra
    rgb, a = cutout("mouse.png", thr=3, dark=0)
    sh = add_shadow(rgb, a, 0.03, 0.02, 0.28)
    m, shp = finish("mouse.png", rgb, a, sh, 300)
    print("mouse.png", shp, m)
    rgb, a = cutout("tape.png", thr=3, dark=0)
    sh = add_shadow(rgb, a)
    m, shp = finish("tape.png", rgb, a, sh, 720)
    src = save
    print("tape.png", shp, m)
mouse(os.path.join(os.path.dirname(os.path.abspath(src)), "extra"))
