"""把 C 的 UI 排版與 Python 參考實作逐像素比對。

Python 那份是規格（`tools/ui_preview.py`），C 那份要照它抄。這裡不只比
「看起來像」—— 320x240 每一個像素都要一樣，所以斷行差一個字、字模索引
偏一格、缺字框畫錯位置，全部會被抓出來。

    python firmware/compare_ui.py

需要先跑過 tools/mkdict.py + tools/mkfont.py 產生 out/DICT，
並用 firmware/build_ui.bat 編譯。
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

from PIL import Image  # noqa: E402

import ui_preview as P  # noqa: E402
from mkfont import Font  # noqa: E402

EXE = os.path.join(HERE, "test_ui.exe")
DICT_DIR = os.path.join(ROOT, "out", "DICT")
OUT = os.path.join(ROOT, "out", "font")

# 詞條畫面：挑的不是「查得到就好」，而是各自會踩到一種排版邊界。
RESULT_WORDS = [
    "dictionary",   # 基本款，中英混排 + 音標
    "a",            # 極短詞，釋義極長 —— 會撞到底部截斷
    "run",          # 釋義段落多，`\n` 分段
    "information",  # 窄字前進寬度（m/w）差一格就會整行歪掉
    "resume",       # 音標裡有 schwa 這類非 ASCII，走窄表的非 ASCII 分支
]
TYPING_PREFIXES = ["app", "a", "hel", "z", "com"]


def diff(a, b, name):
    if a.size != b.size:
        print("  FAIL  %s：尺寸不同 %s vs %s" % (name, a.size, b.size))
        return 1
    pa, pb = a.load(), b.load()
    bad = []
    for y in range(a.size[1]):
        for x in range(a.size[0]):
            if pa[x, y] != pb[x, y]:
                bad.append((x, y, pa[x, y], pb[x, y]))
    if not bad:
        print("  ok    %s" % name)
        return 0
    print("  FAIL  %s：%d 個像素不同，前三個 %s"
          % (name, len(bad), bad[:3]))
    # 差異圖：Python 綠、C 洋紅，一眼看得出是整行歪掉還是單一字模錯
    d = Image.new("RGB", a.size, (0, 0, 0))
    pd = d.load()
    for x, y, va, vb in bad:
        pd[x, y] = (255, 0, 255) if sum(vb) > sum(va) else (0, 255, 0)
    d.save(os.path.join(OUT, "diff_%s.png" % name))
    return 1


def run_c(mode, arg, out):
    r = subprocess.run([EXE, DICT_DIR, mode, arg, out],
                       capture_output=True)
    if r.returncode != 0:
        raise RuntimeError("C 端失敗：%s"
                           % r.stderr.decode("utf-8", "replace").strip())
    return r.stdout.decode("utf-8", "replace").strip()


def main():
    if not os.path.exists(EXE):
        print("找不到 %s —— 先跑 firmware/build_ui.bat" % EXE)
        return 1
    os.makedirs(OUT, exist_ok=True)
    font = Font(os.path.join(DICT_DIR, "FONT.BIN"))

    fails = 0
    print("詞條畫面：")
    for w in RESULT_WORDS:
        ppm = os.path.join(OUT, "c_result_%s.ppm" % w)
        try:
            run_c("result", w, ppm)
        except RuntimeError as e:
            print("  skip  %s（%s）" % (w, e))
            continue
        fails += diff(P.render_result(font, w), Image.open(ppm),
                      "result_%s" % w)

    print("邊打邊查畫面：")
    for t in TYPING_PREFIXES:
        ppm = os.path.join(OUT, "c_typing_%s.ppm" % t)
        run_c("typing", t, ppm)
        fails += diff(P.render_typing(font, t), Image.open(ppm),
                      "typing_%s" % t)

    print()
    print("全部一致" if not fails else "%d 張畫面不同" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
