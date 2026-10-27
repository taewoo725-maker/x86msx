#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mkicons_hd.py - 2.17 상태 아이콘 (부드러운 가장자리). kernel/icons_hd.c를 만든다.
아이콘마다 48x48 알파 두 장: [0] 바탕 그림(상태 색), [1] 강조(밝은 색, 불·릴·손잡이 등).
화면에서는 원하는 크기로 줄여(면적 평균) 카드 색 위에 섞어 그린다. 차례는 kernel/icons.h의 ICON_*와 같다."""
import os, math
from PIL import Image, ImageDraw, ImageFont
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
N, K = 48, 8
SZ = N * K
CJK = '/usr/share/fonts/opentype/noto/NotoSansCJK-Black.ttc'
INTER = '/usr/share/fonts/opentype/inter/Inter-Black.otf'

class Ic:
    def __init__(s):
        s.m = Image.new('L', (SZ, SZ), 0); s.a = Image.new('L', (SZ, SZ), 0)
        s.dm = ImageDraw.Draw(s.m); s.da = ImageDraw.Draw(s.a)
def P(*v): return [int(round(x * K)) for x in v]
def rr(d, x0, y0, x1, y1, r, f=255): d.rounded_rectangle(P(x0, y0, x1, y1), radius=int(r * K), fill=f)
def rect(d, x0, y0, x1, y1, f=255): d.rectangle(P(x0, y0, x1, y1), fill=f)
def circ(d, cx, cy, r, f=255): d.ellipse(P(cx - r, cy - r, cx + r, cy + r), fill=f)
def poly(d, pts, f=255): d.polygon([tuple(P(x, y)) for x, y in pts], fill=f)
def line(d, pts, w, f=255):
    d.line([tuple(P(x, y)) for x, y in pts], fill=f, width=int(w * K), joint='curve')
    for x, y in (pts[0], pts[-1]): circ(d, x, y, w / 2, f)
def text(d, s, cx, cy, size, font, f=0):
    ft = ImageFont.truetype(font, int(size * K))
    b = d.textbbox((0, 0), s, font=ft)
    d.text((cx * K - (b[0] + b[2]) / 2, cy * K - (b[1] + b[3]) / 2), s, font=ft, fill=f)

def keycap(ic):                                     # 키 모양: 바깥 몸통 + 윗면
    rr(ic.dm, 3, 5, 45, 45, 9)
    rr(ic.dm, 7, 8, 41, 37, 6, 0)                   # 윗면 테두리 (살짝 빈 자리)
    rr(ic.dm, 9, 10, 39, 35, 5)
def led(ic, cx=38, cy=14, r=3.6):
    circ(ic.dm, cx, cy, r + 2, 0); circ(ic.da, cx, cy, r)

def caps(ic):
    keycap(ic)
    poly(ic.dm, [(24, 13), (33, 23), (28, 23), (28, 30), (20, 30), (20, 23), (15, 23)], 0)   # ↑ (빈 자리)
    led(ic, 37, 16, 2.6)
def kana(ic):
    keycap(ic); text(ic.dm, 'カ', 22.5, 23, 21, CJK, 0); led(ic, 37, 16, 2.6)
def hangul(ic):
    keycap(ic); text(ic.dm, '한', 22.5, 23, 20, CJK, 0); led(ic, 37, 16, 2.6)
def fdd(ic):
    poly(ic.dm, [(6, 9), (36, 9), (42, 15), (42, 39)], 0)
    rr(ic.dm, 5, 5, 43, 43, 4)
    poly(ic.dm, [(36, 4), (44, 4), (44, 12)], 0)     # 모서리 깎기
    rect(ic.dm, 14, 5, 34, 18, 0)                    # 셔터 자리
    rr(ic.da, 15, 5, 33, 17, 1.5)                    # 금속 셔터
    rect(ic.da, 27, 7, 31, 15, 0)                    # 셔터 구멍
    rr(ic.dm, 11, 25, 37, 43, 2.5, 0)                # 라벨
    rr(ic.da, 13, 27, 35, 43, 1.5)
    for y in (32, 37): rect(ic.da, 16, y, 32, y + 1.6, 0)
def cas(ic):
    rr(ic.dm, 3, 9, 45, 40, 5)
    rr(ic.dm, 9, 14, 39, 28, 4, 0)                   # 창
    for cx in (17, 31):
        circ(ic.da, cx, 21, 4.6); circ(ic.da, cx, 21, 1.8, 0)
    rect(ic.da, 21, 18.5, 27, 23.5)                  # 테이프
    poly(ic.dm, [(12, 40), (15, 33), (33, 33), (36, 40)], 0)
    poly(ic.dm, [(14, 40), (16.5, 35), (31.5, 35), (34, 40)])
    for cx in (20, 28): circ(ic.dm, cx, 37.5, 1.3, 0)
def hd(ic):
    rr(ic.dm, 5, 6, 43, 42, 6)
    circ(ic.dm, 22, 22, 13, 0); circ(ic.dm, 22, 22, 11)
    circ(ic.dm, 22, 22, 3.2, 0); circ(ic.da, 22, 22, 2)
    line(ic.dm, [(36, 12), (28, 26)], 3, 0); line(ic.da, [(36, 12), (28, 26)], 1.6)
    circ(ic.dm, 36, 37, 3, 0); circ(ic.da, 36, 37, 2.2)
    for cx in (10, 14): circ(ic.dm, cx, 37, 1.2, 0)
def cart(ic):
    poly(ic.dm, [(9, 4), (39, 4), (42, 8), (42, 38), (6, 38), (6, 8)])
    rr(ic.dm, 11, 8, 37, 26, 2.5, 0)                 # 라벨 홈
    rr(ic.da, 13, 10, 35, 24, 1.5)
    text(ic.da, 'MSX', 24, 17, 8.5, INTER, 0)
    rect(ic.dm, 9, 38, 39, 45)                       # 단자
    for i in range(7): rect(ic.dm, 10.5 + i * 4, 40, 12.5 + i * 4, 45, 0)
    for i in range(7): rect(ic.da, 10.5 + i * 4, 40.5, 12.5 + i * 4, 44.5)
def switch(ic):
    rr(ic.dm, 3, 14, 45, 34, 10)
    rr(ic.dm, 6, 17, 42, 31, 7, 0)
    circ(ic.da, 33, 24, 8)
    circ(ic.dm, 15, 24, 2.2)
def mixer(ic):
    for x, ky in ((10, 30), (24, 15), (38, 25)):
        rr(ic.dm, x - 1.6, 5, x + 1.6, 43, 1.6)
        rr(ic.dm, x - 6, ky - 4.5, x + 6, ky + 4.5, 2.5, 0)
        rr(ic.da, x - 5, ky - 3.5, x + 5, ky + 3.5, 2)
        rect(ic.da, x - 3.5, ky - 0.6, x + 3.5, ky + 0.6, 0)
def keyboard(ic):
    rr(ic.dm, 2, 11, 46, 38, 5)
    for r, (y, n, x0) in enumerate(((15, 9, 6), (21.5, 9, 7.8), (28, 8, 9.6))):
        for i in range(n): rr(ic.dm, x0 + i * 4.2, y, x0 + i * 4.2 + 2.9, y + 3.8, 0.7, 0)
    rr(ic.dm, 14, 33, 34, 35.6, 0.8, 0)
    rr(ic.da, 14.6, 33.4, 33.4, 35.2, 0.6)
def gear(ic):
    teeth = 8
    pts = []
    for i in range(teeth * 4):
        a = (i / (teeth * 4)) * 2 * math.pi
        rad = 20 if (i % 4) in (0, 1) else 15
        a2 = a + (math.pi / (teeth * 4)) * (0.35 if (i % 4) in (0, 2) else -0.35)
        pts.append((24 + rad * math.cos(a2), 24 + rad * math.sin(a2)))
    poly(ic.dm, pts)
    circ(ic.dm, 24, 24, 7.5, 0); circ(ic.da, 24, 24, 4.5)
def ic_body(ic):                                    # 음원 칩: 몸통 + 다리
    rr(ic.dm, 7, 8, 41, 40, 4)
    for i in range(5):
        y = 11 + i * 6.4
        rr(ic.dm, 2, y, 7.5, y + 3, 1); rr(ic.dm, 40.5, y, 46, y + 3, 1)
    circ(ic.dm, 12.5, 13, 2, 0)
def wave(ic, pts, w=3.2): line(ic.da, pts, w); line(ic.dm, pts, w + 2.6, 0); line(ic.da, pts, w)
def psg(ic):
    ic_body(ic); wave(ic, [(12, 30), (12, 19), (20, 19), (20, 30), (28, 30), (28, 19), (36, 19), (36, 30)])
def psg2(ic):                                       # 3.6.7 Joyrex PSG (두 번째 PSG): PSG 물결 + 2
    ic_body(ic); wave(ic, [(12, 28), (12, 18), (18, 18), (18, 28), (24, 28), (24, 18), (30, 18), (30, 28)])
    text(ic.dm, '2', 35.5, 31, 14, INTER, 0)
def scc(ic):
    ic_body(ic); wave(ic, [(11, 30), (19, 19), (19, 30), (27, 19), (27, 30), (36, 19), (36, 30)])
def pcm(ic):
    ic_body(ic)
    for i, h in enumerate((5, 10, 14, 8, 12, 6)):
        x = 12.5 + i * 4.4
        rr(ic.dm, x - 1.8, 24.5 - h / 2 - 1.2, x + 1.8, 24.5 + h / 2 + 1.2, 1.2, 0)
        rr(ic.da, x - 1, 24.5 - h / 2, x + 1, 24.5 + h / 2, 1)
def fm(ic, mark):
    ic_body(ic)
    pts = [(11 + i * 0.5, 24.5 - 6 * math.sin(i * 0.5 / 25 * 2 * math.pi * 1.5) * (1 if True else 0)) for i in range(51)]
    wave(ic, pts, 3)
    if mark: text(ic.dm, mark, 34, 34.5, 7, INTER, 0)
def midi(ic):
    circ(ic.dm, 24, 24, 20); circ(ic.dm, 24, 24, 16.5, 0); circ(ic.dm, 24, 24, 14.5)
    for a in (180, 225, 270, 315, 360):
        r = math.radians(a); circ(ic.dm, 24 + 9 * math.cos(r), 26 + 9 * math.sin(r), 2.3, 0)
        circ(ic.da, 24 + 9 * math.cos(r), 26 + 9 * math.sin(r), 1.5)
    rect(ic.dm, 21, 6, 27, 9.5, 0)

def folder(ic):                                     # 3.0.1: 파일 선택의 폴더
    rr(ic.dm, 3, 9, 22, 20, 3)                      # 탭
    rr(ic.dm, 3, 13, 45, 41, 4)                     # 뒤판
    rr(ic.da, 3, 19, 45, 41, 4)                     # 앞판 (밝게)
def file_(ic):                                      # 3.0.1: 파일
    poly(ic.dm, [(9, 3), (29, 3), (39, 13), (39, 45), (9, 45)])
    rr(ic.dm, 9, 3, 39, 45, 3)
    poly(ic.dm, [(28, 2), (40, 2), (40, 14)], 0)    # 접힌 모서리
    poly(ic.dm, [(28, 3), (28, 14), (39, 14)], 0)
    poly(ic.da, [(29, 4), (29, 13), (38, 13)])
    for y in (21, 27, 33, 39): rr(ic.da, 14, y, 34 if y < 39 else 26, y + 2.4, 1.2)
def text_ic(ic):                                    # 4.0.29: 텍스트 파일 (txt·doc·bat·bas·ini) — 글자 'Aa'가 든 쪽 + 글줄
    poly(ic.dm, [(9, 3), (29, 3), (39, 13), (39, 45), (9, 45)])
    rr(ic.dm, 9, 3, 39, 45, 3)
    poly(ic.dm, [(28, 2), (40, 2), (40, 14)], 0)
    poly(ic.dm, [(28, 3), (28, 14), (39, 14)], 0)
    poly(ic.da, [(29, 4), (29, 13), (38, 13)])
    text(ic.dm, 'TXT', 24, 24, 10.5, INTER, 0)       # 지워 낸 글자
    for y, e in ((33, 34), (38.5, 28)): rr(ic.da, 14, y, e, y + 2.4, 1.2)
def pointer(ic):                                    # 3.0.1: 마우스 포인터 (끝 = 왼쪽 위). 바탕 = 테두리, 강조 = 안
    pts = [(4, 3), (4, 38), (13, 30), (19, 44), (25, 41), (19, 28), (31, 28)]
    poly(ic.dm, pts); line(ic.dm, pts + [pts[0]], 4)
    poly(ic.da, pts)

# 3.5.0 도구 막대 아이콘 (X86APPLEII 2.7.1에서 가져온 것 + MSX용)
def reset(ic):                                      # 리셋: 돌아가는 화살표
    pts = [(24 + 16 * math.cos(math.radians(a)), 25 + 16 * math.sin(math.radians(a))) for a in range(-60, 241, 6)]
    line(ic.dm, pts, 5.5)
    poly(ic.da, [(31, 4), (36, 15.5), (23.5, 15)])
    line(ic.da, [(31.5, 9), (32.5, 11.5)], 3)
def power(ic):                                      # 전원 기호
    pts = [(24 + 17 * math.cos(math.radians(a)), 26 + 17 * math.sin(math.radians(a))) for a in range(-55, 236, 5)]
    line(ic.dm, pts, 5.5)
    line(ic.da, [(24, 4), (24, 24)], 5.5)
def speed(ic):                                      # 속도계
    pts = [(24 + 19 * math.cos(math.radians(a)), 30 + 19 * math.sin(math.radians(a))) for a in range(180, 361, 6)]
    line(ic.dm, pts, 5)
    for a in (200, 240, 300, 340):
        r = math.radians(a); line(ic.dm, [(24 + 12 * math.cos(r), 30 + 12 * math.sin(r)), (24 + 14 * math.cos(r), 30 + 14 * math.sin(r))], 2.4)
    line(ic.da, [(24, 30), (35, 17)], 4)
    circ(ic.da, 24, 30, 4.5)
    rr(ic.dm, 10, 37, 38, 41, 2)
def monitor(ic):                                    # 모니터: 브라운관 + 화면
    rr(ic.dm, 3, 5, 45, 37, 5)
    rr(ic.dm, 7, 9, 41, 31, 3, 0)
    rr(ic.da, 9, 11, 39, 29, 2)
    for y in (15, 20, 25): rect(ic.da, 12, y, 28 if y != 20 else 33, y + 1.6, 0)
    poly(ic.dm, [(18, 37), (30, 37), (33, 43), (15, 43)])
def joy(ic):                                        # 조이스틱: 받침 + 막대 + 공 + 단추
    rr(ic.dm, 5, 33, 43, 44, 4)
    rr(ic.dm, 21.8, 14, 26.2, 36, 2)
    circ(ic.da, 24, 12, 7.5)
    circ(ic.dm, 36, 31, 3.5); circ(ic.da, 36, 31, 2.5)
def tray(ic): line(ic.dm, [(6, 28), (6, 42), (42, 42), (42, 28)], 5)
def save(ic):                                       # 상태 저장: 상자로 들어가는 화살표
    tray(ic); rr(ic.da, 20.5, 4, 27.5, 22, 2); poly(ic.da, [(12, 19), (36, 19), (24, 33)])
def load(ic):                                       # 상태 불러오기: 상자에서 나오는 화살표
    tray(ic); rr(ic.da, 20.5, 15, 27.5, 34, 2); poly(ic.da, [(12, 18), (36, 18), (24, 4)])
def camera(ic):                                     # 화면 저장: 사진기
    rr(ic.dm, 3, 13, 45, 41, 6)
    rr(ic.dm, 15, 7, 31, 15, 3)
    circ(ic.dm, 24, 27, 10.5, 0); circ(ic.da, 24, 27, 8); circ(ic.da, 24, 27, 4, 0)
    circ(ic.da, 38, 19, 2.2)
def mouse(ic):                                      # MSX 마우스: 몸통 + 가운데 줄 + 왼쪽 단추 + 줄
    rr(ic.dm, 11, 8, 37, 44, 13)
    rect(ic.dm, 23, 8, 25, 24, 0); rect(ic.dm, 11, 22, 37, 24.5, 0)
    rr(ic.da, 14, 11, 21.5, 20.5, 3)
    line(ic.dm, [(24, 8), (24, 3), (32, 3)], 2.6)
def pause(ic):                                      # 멈춤: 막대 둘
    rr(ic.dm, 11, 8, 20, 40, 3); rr(ic.dm, 28, 8, 37, 40, 3)
def menu(ic):                                       # 메뉴: 줄 셋
    for y in (11, 22, 33): rr(ic.dm, 7, y, 41, y + 5, 2.5)
def scan(ic):                                       # 스캔라인: 화면 + 가로줄
    rr(ic.dm, 4, 7, 44, 41, 5)
    for i, y in enumerate(range(12, 38, 5)): rect(ic.da if i % 2 else ic.dm, 8, y, 40, y + 2.4, 255 if i % 2 else 0)
def media(ic):                                      # 미디어 판: 카트리지 + 디스크 겹침
    rr(ic.dm, 4, 4, 30, 40, 4); rect(ic.dm, 9, 9, 25, 20, 0); rr(ic.da, 10.5, 10.5, 23.5, 18.5, 1.5)
    rr(ic.dm, 20, 18, 46, 46, 3, 0); rr(ic.dm, 22, 20, 44, 44, 2.5); rect(ic.dm, 27, 20, 39, 27, 0); rr(ic.da, 26, 33, 40, 42, 1.5)

# 3.7.0 음원 아이콘: OPP(YAMAHA SFG, YM2164), SOFT MIDI(건반 = 소프트웨어 음원), USB MIDI(USB 표시), 기타(빈 칩 — 이름 첫 글자를 화면에서 얹음)
def opp(ic):
    ic_body(ic)
    pts = [(11 + i * 0.5, 21 - 5 * math.sin(i * 0.5 / 25 * 2 * math.pi * 1.5)) for i in range(51)]
    wave(ic, pts, 2.6)
    text(ic.dm, 'OPP', 24, 32.5, 9.5, INTER, 0)
def softmidi(ic):
    rr(ic.dm, 3, 9, 45, 41, 4)
    for i in range(6): rr(ic.da, 5.5 + i * 6.3, 12, 10.6 + i * 6.3, 38.5, 1.2)
    for i in (0, 1, 3, 4): rr(ic.da, 8.9 + i * 6.3, 11, 14.9 + i * 6.3, 27.5, 1, 0)   # 검은 건반 = 바탕색
def usbmidi(ic):
    line(ic.dm, [(24, 7), (24, 38)], 3.6)
    poly(ic.dm, [(24, 2), (30, 10), (18, 10)])
    line(ic.dm, [(24, 31), (13, 24), (13, 17)], 3.2); rr(ic.da, 9.5, 11.5, 16.5, 18.5, 1)
    line(ic.dm, [(24, 26), (35, 19), (35, 14)], 3.2); circ(ic.da, 35, 12, 3.8)
    circ(ic.dm, 24, 41, 5); circ(ic.da, 24, 41, 2.6)
def etc(ic):
    ic_body(ic)
    rr(ic.dm, 13, 14, 35, 34, 3, 0)               # 가운데 빈 자리 (글자를 얹음)

def zipic(ic):                                      # 3.8.2: 압축(.zip) 파일 — 파일 모양 + 가운데 지퍼
    poly(ic.dm, [(9, 3), (29, 3), (39, 13), (39, 45), (9, 45)])
    rr(ic.dm, 9, 3, 39, 45, 3)
    poly(ic.dm, [(28, 2), (40, 2), (40, 14)], 0)
    poly(ic.dm, [(28, 3), (28, 14), (39, 14)], 0)
    poly(ic.da, [(29, 4), (29, 13), (38, 13)])
    for i, y in enumerate(range(5, 28, 4)):         # 지퍼 이빨 (왼쪽·오른쪽 번갈아)
        x = 20 if i % 2 == 0 else 24
        rect(ic.da, x, y, x + 4, y + 2.6)
    rr(ic.dm, 18, 29, 30, 42, 2.5, 0)               # 손잡이 자리
    rr(ic.da, 20, 31, 28, 40, 1.8)
    rect(ic.dm, 22.5, 34, 25.5, 38, 0)
def hdd(ic):                                        # 3.8.2: 하드 디스크 드라이브 (위에서 본 3.5인치) — 몸통 + 접시 + 팔 + 아래 커넥터
    rr(ic.dm, 4, 4, 44, 36, 5)                      # 몸통
    circ(ic.dm, 22, 20, 13.5, 0); circ(ic.dm, 22, 20, 12)      # 접시 테두리 (빈 고리 + 접시)
    circ(ic.dm, 22, 20, 8.5, 0); circ(ic.da, 22, 20, 7.6)      # 접시 안 강조 고리
    circ(ic.dm, 22, 20, 6.2); circ(ic.dm, 22, 20, 2.6, 0); circ(ic.da, 22, 20, 1.5)   # 허브
    line(ic.dm, [(38, 8), (28, 22)], 3.6, 0); line(ic.da, [(38, 8), (28, 22)], 1.8)   # 액추에이터 팔
    circ(ic.dm, 38, 8, 2.4, 0); circ(ic.da, 38, 8, 1.4)
    rr(ic.dm, 8, 39, 40, 45, 2)                     # 커넥터 띠
    for x in range(11, 38, 4): rect(ic.da, x, 41, x + 2, 43.6)   # 핀
    circ(ic.dm, 39, 31, 2.7, 0); circ(ic.da, 39, 31, 1.7)        # 활동 표시등
def printer(ic):                                    # 4.0.3: 프린터 — 급지 용지 + 몸통 + 나오는 용지(글줄) + 불
    rr(ic.dm, 13, 3, 35, 17, 2)                     # 위에서 들어가는 용지
    rr(ic.dm, 4, 15, 44, 35, 5)                     # 몸통
    rr(ic.dm, 10, 28, 38, 46, 2.5, 0)               # 나오는 용지 둘레 (빈 고리)
    rr(ic.dm, 12, 30, 36, 45, 1.5)                  # 나오는 용지
    for y in (33, 37, 41): rect(ic.da, 15, y, 33, y + 1.8)   # 글줄
    circ(ic.dm, 38, 21, 3.2, 0); circ(ic.da, 38, 21, 2.2)    # 표시등
def cpu_chip(ic, label, size, bars):                # 4.0.34: 옛 DIP 칩 — 핀 6개씩 + 윗 홈 + 큰 글자(지워 낸 글자)
    for i in range(6):
        y = 7 + i * 6.8
        rr(ic.dm, 1, y - 1.7, 9, y + 1.7, 1); rr(ic.dm, 39, y - 1.7, 47, y + 1.7, 1)
    rr(ic.dm, 7, 2, 41, 46, 5)                       # 몸통
    circ(ic.dm, 24, 2, 4.5, 0)                       # 윗 홈
    circ(ic.da, 12, 7, 1.6)                          # 1번 핀 표시
    rr(ic.da, 11, 17, 37, 34, 2)                     # 강조 판
    text(ic.da, label, 24, 25.5, size, INTER, 0)
    text(ic.dm, label, 24, 25.5, size, INTER, 0)
    for i in range(bars): rr(ic.da, 14 + i * 7, 38, 19 + i * 7, 41.4, 1.2)
def z80(ic): cpu_chip(ic, 'Z80', 13, 1)
def r800(ic): cpu_chip(ic, 'R800', 9.6, 3)

def stateic(ic):                                    # 4.0.30: 상태 저장 파일(.sta) — 메모리 카드(모서리 깎임) + 금색 접점 + 지워 낸 책갈피
    poly(ic.dm, [(9, 3), (32, 3), (40, 11), (40, 45), (9, 45)])
    rr(ic.dm, 9, 3, 40, 45, 3)
    poly(ic.dm, [(31, 2), (42, 2), (42, 13)], 0)
    for x in (13, 18.5, 24, 29.5): rect(ic.da, x, 5, x + 3.6, 12)          # 접점
    poly(ic.dm, [(18, 20), (31, 20), (31, 38), (24.5, 32.5), (18, 38)], 0)  # 책갈피 (빈 자리)
    poly(ic.da, [(20.5, 22.5), (28.5, 22.5), (28.5, 33), (24.5, 29.5), (20.5, 33)])
def soundic(ic):                                    # 4.0.30: 사운드폰트(.sf2) — 건반
    rr(ic.dm, 3, 10, 45, 38, 4)
    for x in (11.4, 19.8, 28.2, 36.6): rect(ic.dm, x - 0.5, 24, x + 0.5, 38, 0)   # 흰 건반 사이 줄
    for x in (9.5, 17.9, 26.3): pass
    for x0 in (8.4, 16.8, 33.6):
        rect(ic.dm, x0 - 1, 10, x0 + 6.4, 26, 0); rect(ic.da, x0, 10, x0 + 5.4, 25)   # 검은 건반
    rr(ic.da, 6, 40.5, 42, 43, 1.2)
def diskimg(ic):                                    # 4.0.30: 큰 디스크 이미지(1.44 MB 넘는 .dsk) — 디스크 팩(원통) + 줄 둘
    rect(ic.dm, 8, 11, 40, 36)
    ic.dm.ellipse(P(8, 5, 40, 17), fill=255); ic.dm.ellipse(P(8, 30, 40, 42), fill=255)
    ic.dm.arc(P(8, 14, 40, 26), 0, 180, fill=0, width=int(2.6 * K))
    ic.dm.arc(P(8, 22, 40, 34), 0, 180, fill=0, width=int(2.6 * K))
    ic.dm.ellipse(P(13, 7.5, 35, 14.5), fill=0); ic.da.ellipse(P(15.5, 8.8, 32.5, 13.2), fill=255)
def otheric(ic):                                    # 4.0.30: 그 밖의 파일 — 핀 달린 칩 + 지워 낸 '?'
    rr(ic.dm, 11, 5, 37, 43, 3)
    for k in range(5):
        y = 8 + k * 7.2
        rect(ic.dm, 5.5, y, 11, y + 3.2); rect(ic.dm, 37, y, 42.5, y + 3.2)
    circ(ic.dm, 24, 5, 3, 0)                                                # 1번 핀 쪽 홈
    text(ic.dm, '?', 24, 25, 18, INTER, 0)
    circ(ic.da, 15, 38, 1.8)
def dsk360(ic):                                     # 4.0.31: 5.25인치 360 KB 디스크 — 슬리브 + 허브 구멍 + 읽기 홈 + 쓰기 보호 홈 + 라벨에 지워 낸 '360K'
    rr(ic.dm, 5, 5, 43, 43, 3)
    circ(ic.dm, 24, 15.5, 6.5, 0); circ(ic.da, 24, 15.5, 2.8)              # 허브 구멍 + 안쪽 고리
    circ(ic.dm, 24, 15.5, 1.2, 0)
    rr(ic.dm, 21.5, 23.5, 26.5, 30.5, 2.5, 0)                               # 읽기 홈
    rect(ic.dm, 41.5, 9, 44, 15.5, 0)                                       # 쓰기 보호 홈
    rr(ic.da, 9, 33, 39, 41.5, 1.5)
    text(ic.da, '360K', 24, 37.4, 7.6, INTER, 0)
def disk35(ic, label):                              # 3.5인치 디스크 + 라벨 글자 (용량)
    fdd(ic)
    rr(ic.da, 13, 27, 35, 43, 1.5)
    text(ic.da, label, 24, 35.4, 6.6 if len(label) > 4 else 7.8, INTER, 0)
def dsk720(ic): disk35(ic, '720K')                  # 4.0.31
def dsk144(ic): disk35(ic, '1.44M')                 # 4.0.31: 1.44 MB (2HD)
def arrow_r(ic):                                    # 4.0.31: 파일 관리 가운데 복사 방향 화살표
    poly(ic.dm, [(6, 17), (24, 17), (24, 6), (43, 24), (24, 42), (24, 31), (6, 31)])
def arrow_l(ic):
    poly(ic.dm, [(42, 17), (24, 17), (24, 6), (5, 24), (24, 42), (24, 31), (42, 31)])
def picic(ic):                                      # 4.0.31: 그림 파일 (.png 등): 액자 + 산 + 해
    rr(ic.dm, 4, 8, 44, 40, 4); rr(ic.dm, 8, 12, 40, 36, 2, 0)
    poly(ic.da, [(8, 36), (18, 22), (25, 31), (31, 25), (40, 36)]); circ(ic.da, 33, 18, 4)
def movic(ic):                                      # 4.0.31: 영상 파일 (.avi): 필름 + 재생 삼각형
    rr(ic.dm, 8, 4, 40, 44, 3)
    for y in (8, 16, 24, 32, 40):
        rect(ic.dm, 10.5, y, 14.5, y + 3.4, 0); rect(ic.dm, 33.5, y, 37.5, y + 3.4, 0)
    poly(ic.da, [(19, 16), (19, 32), (31, 24)])
def netic(ic):                                      # 인터넷 연결 — 지구본(바깥 원 + 가로·세로 선) + 강조 점
    circ(ic.dm, 24, 24, 20); circ(ic.dm, 24, 24, 16.6, 0)
    ic.dm.ellipse(P(15, 4, 33, 44), outline=255, width=int(2.8 * K))
    rect(ic.dm, 4, 22.6, 44, 25.4); rect(ic.dm, 23, 4, 25, 44, 0)
    line(ic.dm, [(7.5, 14), (40.5, 14)], 2.4); line(ic.dm, [(7.5, 34), (40.5, 34)], 2.4)
    circ(ic.da, 36, 36, 6.2); circ(ic.dm, 36, 36, 7.6, 0); circ(ic.da, 36, 36, 4.6)
def v9990(ic):                                      # V9990 비디오 칩 — 사방에 핀이 있는 네모난 칩 + 큰 글자 + 강조 판
    for i in range(5):
        t = 11 + i * 6.5
        rr(ic.dm, t - 1.5, 1, t + 1.5, 9, 1); rr(ic.dm, t - 1.5, 39, t + 1.5, 47, 1)
        rr(ic.dm, 1, t - 1.5, 9, t + 1.5, 1); rr(ic.dm, 39, t - 1.5, 47, t + 1.5, 1)
    rr(ic.dm, 7, 7, 41, 41, 4)                       # 몸통
    circ(ic.da, 12, 12, 1.8)                         # 1번 핀 표시
    rr(ic.da, 10, 17, 38, 32, 2)                     # 강조 판
    text(ic.da, 'V9990', 24, 24.5, 8.6, INTER, 0)
    text(ic.dm, 'V9990', 24, 24.5, 8.6, INTER, 0)
    for i in range(3): rr(ic.da, 15 + i * 6, 35, 19 + i * 6, 37.6, 1)
ORDER = [('CAPS', caps), ('KANA', kana), ('FDD', fdd), ('CAS', cas), ('HD', hd), ('CART', cart), ('SWITCH', switch),
         ('MIXER', mixer), ('KEYBOARD', keyboard), ('GEAR', gear), ('PSG', psg), ('SCC', scc), ('PCM', pcm),
         ('MM', lambda i: fm(i, '')), ('MA', lambda i: fm(i, 'A')), ('MS', lambda i: fm(i, 'S')), ('OPP', opp),
         ('MIDI', midi), ('HANGUL', hangul), ('FOLDER', folder), ('FILE', file_), ('POINTER', pointer),
         ('RESET', reset), ('POWER', power), ('SPEED', speed), ('MONITOR', monitor), ('JOY', joy), ('SAVE', save), ('LOAD', load),
         ('CAMERA', camera), ('MOUSE', mouse), ('PAUSE', pause), ('MENU', menu), ('SCAN', scan), ('MEDIA', media), ('PSG2', psg2),
         ('SOFTMIDI', softmidi), ('USBMIDI', usbmidi), ('ETC', etc), ('ZIP', zipic), ('HDD', hdd), ('PRINTER', printer), ('Z80', z80), ('R800', r800), ('TEXT', text_ic), ('STATE', stateic), ('SOUND', soundic), ('DISKIMG', diskimg), ('OTHER', otheric), ('DSK360', dsk360), ('DSK144', dsk144), ('DSK720', dsk720), ('ARROWR', arrow_r), ('ARROWL', arrow_l), ('PICTURE', picic), ('MOVIE', movic), ('NET', netic), ('V9990', v9990)]
COLORS = {'FOLDER': ((214, 158, 30), (251, 199, 70)), 'FILE': ((200, 206, 218), (76, 141, 255)), 'POINTER': ((5, 7, 11), (255, 255, 255))}   # PNG 색 (바탕, 강조)

PNGNAME = {'MM': 'M-M', 'MA': 'M-A', 'MS': 'M-S'}             # 파일 이름 = kernel/icons.c icon_name

def main():
    enum = open(os.path.join(ROOT, 'kernel', 'icons.h'), encoding='utf-8').read().split('enum {')[1].split('}')[0]
    names = [e.strip()[5:] for e in enum.replace('\n', ' ').split(',') if e.strip().startswith('ICON_') and 'COUNT' not in e]
    assert names == [n for n, _ in ORDER], names
    out = os.path.join(ROOT, 'kernel', 'icons_hd.c')
    prev = Image.new('RGB', (len(ORDER) * 56, 112), (22, 28, 41))
    with open(out, 'w', encoding='utf-8') as f:
        f.write('/* icons_hd.c - tools/mkicons_hd.py가 만든 상태 아이콘 (48x48 알파 2장: 바탕, 강조) */\n#include <stdint.h>\n#include "icons.h"\n\n')
        f.write('const uint8_t icon_hd[ICON_COUNT][2][48 * 48 / 2] = {   /* 4비트 알파, 한 바이트에 두 점 (앞 점 = 아래 4비트) */\n')
        for k, (n, fn) in enumerate(ORDER):
            ic = Ic(); fn(ic)
            m = ic.m.resize((N, N), Image.BOX); a = ic.a.resize((N, N), Image.BOX)
            f.write(f'    {{ /* {n} */\n')
            for L in (m, a):
                b = L.tobytes(); q = [(v * 15 + 127) // 255 for v in b]
                f.write('      {' + ','.join(str(q[i] | q[i + 1] << 4) for i in range(0, len(q), 2)) + '},\n')
            f.write('    },\n')
            # 3.0: USB Icons 폴더용 PNG (48x48, 투명 배경, 바탕 = 밝은 글자색, 강조 = 노랑) — 사용자가 바꿔 쓸 수 있음
            png = Image.new('RGBA', (N, N), (0, 0, 0, 0))
            M_, A_ = COLORS.get(n, ((230, 234, 242), (251, 191, 36)))
            for yy in range(N):
                for xx in range(N):
                    am = m.getpixel((xx, yy)) / 255; aa = a.getpixel((xx, yy)) / 255
                    oa = aa + am * (1 - aa)
                    if oa <= 0: continue
                    c = tuple(int(round((A_[i] * aa + M_[i] * am * (1 - aa)) / oa)) for i in range(3))
                    png.putpixel((xx, yy), c + (int(round(oa * 255)),))
            png.save(os.path.join(ROOT, 'assets', 'icons', PNGNAME.get(n, n) + '.png'))
            for row, (mc, ac) in enumerate((((230, 234, 242), (251, 191, 36)), ((136, 146, 166), (82, 93, 115)))):
                for yy in range(N):
                    for xx in range(N):
                        am = ((m.getpixel((xx, yy)) * 15 + 127) // 255) / 15; aa = ((a.getpixel((xx, yy)) * 15 + 127) // 255) / 15
                        c = [22, 28, 41]
                        c = [c[i] * (1 - am) + mc[i] * am for i in range(3)]
                        c = [c[i] * (1 - aa) + ac[i] * aa for i in range(3)]
                        prev.putpixel((k * 56 + 4 + xx, row * 56 + 4 + yy), tuple(int(v) for v in c))
        f.write('};\n')
    prev.save(os.path.join(os.environ.get('TMPDIR', '/tmp'), 'icons_preview.png'))
    print('wrote', out)
main()
