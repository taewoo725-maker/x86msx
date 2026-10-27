# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
import socket,sys,time
s=socket.socket(socket.AF_UNIX); s.connect('/tmp/qm.sock'); time.sleep(0.3); s.recv(65536)
for c in sys.argv[1:]:
    if c.startswith('sleep:'): time.sleep(float(c[6:])); continue
    s.sendall((c+'\n').encode()); time.sleep(0.8)
    try: print(s.recv(65536).decode(errors='replace').strip()[-200:])
    except Exception: pass
