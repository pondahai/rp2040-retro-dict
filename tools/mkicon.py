#!/usr/bin/env python3
"""產生 rp2040-retro-loader 選單用的封面圖 `RetroDict.ino.RAW`。

    python tools/mkicon.py [來源圖] [rp2040-retro-loader 的路徑]

預設來源是 `assets/retrodict_icon.png`（已裁成正方形的版本）。給一張新的原圖
時會先置中裁成正方形再存回去，所以下次不必再裁一次。

格式是載入器定的：96x96 RGB565 **big-endian**、無標頭、**固定 18432 bytes**。
載入器沒有標頭可以驗，只用檔案長度擋「拖錯檔案」，所以長度必須剛好。
big-endian 是因為 ILI9341 線上格式就是高位元組先送 —— 載入器可以把從 SD
讀到的磁區原封不動丟給 SPI，中間不必逐像素轉換。

轉檔本身呼叫載入器自己那支 `tools/make_thumb.py`（它是規格的正本），
轉完再**把 .RAW 解回圖**確認：長度、尺寸、以及位元組順序真的是 big-endian。
只看 make_thumb 印出來的那行字不算驗過 —— 那只是它自己說它做了什麼。

檔名規則：`<uf2 檔名去副檔名>.RAW`，也就是 `RetroDict.ino.uf2` 配
`RetroDict.ino.RAW`，兩個都放 SD 卡**根目錄**（載入器不支援子目錄）。
"""

import os
import struct
import subprocess
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
ASSETS = os.path.join(ROOT, "assets")
SQUARE = os.path.join(ASSETS, "retrodict_icon.png")
SQUARE_MAX = 768          # 進版控的來源圖上限；96px 的縮圖用不到更大
RAW = os.path.join(ASSETS, "RetroDict.ino.RAW")
CHECK = os.path.join(ASSETS, "retrodict_icon_96.png")

W = H = 96
EXPECT = W * H * 2


def make_square(src_path):
    """置中裁成正方形。原圖是寬幅的，直接丟給 --fit 會把書的上下邊緣切掉。"""
    im = Image.open(src_path).convert("RGB")
    w, h = im.size
    if w == h:
        return im
    side = min(w, h)
    half = int(side * 0.98) // 2          # 留一點邊，圖示不要頂到框
    cx, cy = w // 2, h // 2
    box = (max(0, cx - half), max(0, cy - half),
           min(w, cx + half), min(h, cy + half))
    print("裁切 %s -> %s" % (im.size, (box[2] - box[0], box[3] - box[1])))
    im = im.crop(box)
    if im.size[0] > SQUARE_MAX:
        im = im.resize((SQUARE_MAX, SQUARE_MAX), Image.LANCZOS)
    return im


def verify():
    """把 .RAW 解回圖 —— 驗的是載入器真正會讀到的那些 byte。"""
    data = open(RAW, "rb").read()
    if len(data) != EXPECT:
        print("FAIL  長度 %d，應該是 %d —— 載入器只用長度驗，會直接拒收"
              % (len(data), EXPECT))
        return False
    im = Image.new("RGB", (W, H))
    px = im.load()
    for i in range(W * H):
        v = struct.unpack_from(">H", data, i * 2)[0]     # big-endian
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        px[i % W, i // W] = (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)
    im.resize((W * 4, H * 4), Image.NEAREST).save(CHECK)

    # 位元組順序錯了圖不會壞成馬賽克，只會整張變色 —— 所以這裡比的是
    # 「兩種順序解出來哪一種比較接近來源圖」，不是「看起來像不像圖」。
    ref = Image.open(SQUARE).convert("RGB").resize((W, H), Image.LANCZOS)
    swapped = Image.frombytes("RGB", (W, H), bytes(
        _decode(data, swap=True)))
    if _dist(im, ref) > _dist(swapped, ref):
        print("FAIL  位元組順序看起來是 little-endian，載入器要 big-endian")
        return False
    print("OK    %d bytes、%dx%d、big-endian（解回來與來源圖相符）" %
          (len(data), W, H))
    print("      對照圖：%s" % os.path.normpath(CHECK))
    return True


def _decode(data, swap):
    out = bytearray()
    for i in range(W * H):
        v = struct.unpack_from("<H" if swap else ">H", data, i * 2)[0]
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        out += bytes((r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2))
    return out


def _dist(a, b):
    pa, pb = a.load(), b.load()
    return sum(abs(pa[x, y][c] - pb[x, y][c])
               for y in range(H) for x in range(W) for c in range(3))


def main(argv):
    src = argv[1] if len(argv) > 1 else SQUARE
    loader = argv[2] if len(argv) > 2 else os.path.join(ROOT, "..",
                                                        "rp2040-retro-loader")
    thumb = os.path.join(loader, "tools", "make_thumb.py")
    if not os.path.exists(thumb):
        print("找不到 %s —— 把 rp2040-retro-loader 的路徑當第二個參數傳進來"
              % thumb)
        return 1
    if not os.path.exists(src):
        print("找不到來源圖 %s" % src)
        return 1

    os.makedirs(ASSETS, exist_ok=True)
    sq = make_square(src)
    sq.save(SQUARE)
    print("來源正方圖：%s（%dx%d）" % (os.path.normpath(SQUARE), *sq.size))

    r = subprocess.run([sys.executable, thumb, SQUARE, "--fit", "-o", RAW],
                       capture_output=True)
    out = (r.stdout + r.stderr).decode("utf-8", "replace").strip()
    if out:
        print(out)
    if r.returncode != 0:
        return 1
    return 0 if verify() else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
