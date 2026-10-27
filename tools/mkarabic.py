#!/usr/bin/env python3
# 만든이: KimTaeWoo
# 도와준이: Lee.J.H
# X86MSX - Copyright (C) 2026 Kim Taewoo. SPDX-License-Identifier: GPL-3.0-or-later (see LICENSE)
"""mkarabic.py - 3.4.3 LANG3-01: 아랍어 화면 문구 → kernel/lang_ar.h
글자 모양 잇기(표현형 U+FB50~FEFF)와 오른쪽→왼쪽 순서를 여기서 미리 해 둔다(본체는 그대로 그리기만).
빌드 때만 arabic_reshaper(MIT)·python-bidi를 쓴다 (pip install arabic-reshaper python-bidi).
글꼴은 tools/mkfont.py가 DejaVu Sans에서 아랍어 글자를 그려 넣는다."""
import os, sys
import arabic_reshaper
from bidi.algorithm import get_display
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AR = {
 'S_SP_IDE': 'قرص صلب IDE - Sunrise', 'S_M_FILE': 'ملف', 'S_M_RUN': 'تشغيل', 'S_M_OPTIONS': 'خيارات', 'S_M_TOOLS': 'أدوات', 'S_M_HELP': 'مساعدة',
 'S_CART1': 'فتحة الخرطوشة 1', 'S_CART2': 'فتحة الخرطوشة 2', 'S_DISK_A': 'محرك الأقراص المرنة A', 'S_DISK_B': 'محرك الأقراص المرنة B',
 'S_INSERT': 'إدخال', 'S_EJECT': 'إخراج', 'S_INSERT_SPECIAL': 'إدخال خرطوشة خاصة', 'S_RESET_AFTER_INSERT': 'إعادة الضبط بعد الإدخال',
 'S_REBOOT_PC': 'إعادة تشغيل الحاسوب', 'S_EXIT': 'خروج وإيقاف التشغيل', 'S_RUN_PAUSE': 'تشغيل / إيقاف مؤقت',
 'S_HARD_RESET': 'إعادة ضبط كاملة', 'S_SOFT_RESET': 'إعادة ضبط برمجية', 'S_FULL_RESET': 'إعادة ضبط شاملة',
 'S_EMULATION': 'المحاكاة', 'S_VIDEO': 'الفيديو', 'S_EFFECTS': 'التأثيرات', 'S_SOUND': 'الصوت', 'S_LANGUAGE': 'اللغة',
 'S_MIXER_MENU': 'الخلاط...', 'S_KB_MENU': 'لوحة المفاتيح...', 'S_JOY_MENU': 'عصا التحكم...', 'S_MOUSE_MENU': 'الفأرة...',
 'S_USB_GROW': 'استخدام سعة USB كاملة', 'S_DIAG': 'التشخيص', 'S_ABOUT': 'حول X86MSX', 'S_PAUSED': 'متوقف مؤقتًا', 'S_STOPPED': 'متوقف',
 'S_VOLUME': 'مستوى الصوت', 'S_MUTED': 'صامت', 'S_SPEED': 'السرعة', 'S_DRIVES': 'المحركات',
 'S_COL_NAME': 'اسم الملف', 'S_COL_TYPE': 'النوع', 'S_COL_MAPPER': 'المخطط', 'S_COL_SIZE': 'الحجم', 'S_COL_DATE': 'التاريخ',
 'S_PROPS': 'الخصائص', 'S_TAB_EMU': 'المحاكاة', 'S_TAB_MIXER': 'الخلاط', 'S_TAB_KEYBOARD': 'لوحة المفاتيح', 'S_TAB_JOY': 'عصا التحكم', 'S_TAB_MOUSE': 'الفأرة',
 'S_WRITE_PROTECT': 'حماية من الكتابة', 'S_MONITOR': 'نوع الشاشة', 'S_SCANLINES': 'خطوط المسح', 'S_OUTPUT': 'جهاز الإخراج', 'S_MSX_MACHINE': 'طراز MSX', 'S_NONE': 'لا شيء',
 'S_LANG_AR': 'العربية',
}
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ar_more import AR2                                 # 3.4.4: 나머지 문구 (명령 줄은 [(키, 설명)])
def visual(t):
    return get_display(arabic_reshaper.reshape(t), base_dir='R')
def esc(t):
    return '"' + t.replace('\\', '\\\\').replace('"', '\\"') + '"'
def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'kernel', 'lang_ar.h')
    L = ['/* lang_ar.h - tools/mkarabic.py가 만듦 (고치지 말 것). 아랍어 문구: 표현형으로 이어 쓰고 왼쪽부터 그릴 순서로 */',
         '#ifndef MSX_LANG_AR_H', '#define MSX_LANG_AR_H', '#include "lang2.h"', 'static const LangPair lang_ar[] = {']
    for k, v in list(AR.items()) + list(AR2.items()):
        if k == 'S_LANG_AR':
            continue
        if isinstance(v, list):                         # 명령 줄: "키 설명   키 설명" (키는 그대로, 순서는 ui_cmdbar가 오른쪽부터)
            L.append('    { %s, %s },' % (k, esc('   '.join(key + ' ' + visual(d) for key, d in v))))
        else:
            L.append('    { %s, %s },' % (k, esc(visual(v))))
    L += ['};', '#define LANG_AR_NAME ' + esc(visual(AR['S_LANG_AR'])), '#endif', '']
    open(out, 'w', encoding='utf-8').write('\n'.join(L))
    print(out, len(AR) - 1 + len(AR2), 'strings')
if __name__ == '__main__':
    main()
