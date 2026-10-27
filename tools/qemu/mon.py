# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
import socket, time, sys
s=socket.socket(socket.AF_UNIX); s.settimeout(3); s.connect('/tmp/qm.sock'); s.settimeout(0.3)
def cmd(c,w=0.2):
    s.sendall((c+'\n').encode()); time.sleep(w)
    try: s.recv(65536)
    except Exception: pass
for a in sys.argv[1:]:
    if a.startswith('sleep:'): time.sleep(float(a[6:]))
    elif a.startswith('shot:'): cmd('screendump '+a[5:], 1.0)
    else: cmd('sendkey '+a, 0.25)
