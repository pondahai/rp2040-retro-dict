#!/usr/bin/env python3
"""跑一段按鍵腳本，把每個 [SNAP] 的畫面拼成一張連續圖。

    python tools/ui_session.py "app[SNAP][DOWN][SNAP][ENTER][SNAP]"

畫面是 `firmware/test_app.exe` 產生的 —— 也就是板子上跑的同一份狀態機、
同一份鍵盤解碼、同一塊 4bpp 畫布。按鍵也不是直接餵事件，而是反查成矩陣
座標再逐個時間刻度掃進去，所以去彈跳與修飾鍵都真的走過。

腳本語法：可列印字元照打，特殊鍵寫成 [ENTER] [BS] [ESC] [UP] [DOWN]
[PGUP] [PGDN] [F1]，[SNAP] 存一張圖。
"""

import os
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
EXE = os.path.join(ROOT, "firmware", "test_app.exe")
DICT_DIR = os.path.join(ROOT, "out", "DICT")
OUT = os.path.join(ROOT, "out", "ui")

DEFAULT = ("app[SNAP][DOWN][DOWN][SNAP][ENTER][SNAP][PGDN][SNAP]"
           "[ESC][BS][BS]ple[SNAP][ENTER][SNAP]")


def main(argv):
    script = argv[1] if len(argv) > 1 else DEFAULT
    cols = int(argv[2]) if len(argv) > 2 else 3
    if not os.path.exists(EXE):
        print("找不到 %s —— 先跑 firmware/build_app.bat" % EXE)
        return 1
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.startswith("sheet") or f.endswith(".ppm"):
            os.remove(os.path.join(OUT, f))

    prefix = os.path.join(OUT, "f")
    r = subprocess.run([EXE, DICT_DIR, "run", script, prefix],
                       capture_output=True)
    out = r.stdout.decode("utf-8", "replace")
    print(out.rstrip())
    if r.returncode != 0:
        print(r.stderr.decode("utf-8", "replace").rstrip())
        return 1

    shots = sorted(f for f in os.listdir(OUT)
                   if f.startswith("f") and f.endswith(".ppm"))
    if not shots:
        print("腳本裡沒有 [SNAP]，沒有畫面可拼")
        return 1

    imgs = [Image.open(os.path.join(OUT, f)) for f in shots]
    w, h = imgs[0].size
    gap = 10
    rows = (len(imgs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w + (cols - 1) * gap,
                              rows * h + (rows - 1) * gap), (0, 0, 0))
    for i, im in enumerate(imgs):
        sheet.paste(im, ((i % cols) * (w + gap), (i // cols) * (h + gap)))
    path = os.path.join(OUT, "sheet.png")
    sheet.save(path)
    print("寫出 %s（%d 張畫面）" % (os.path.normpath(path), len(imgs)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
