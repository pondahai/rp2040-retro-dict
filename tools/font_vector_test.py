#!/usr/bin/env python3
"""D3：向量字光柵化到小尺寸，到底能不能用？

    python tools/font_vector_test.py

Google 的 Noto Sans TC 是 SIL OFL 1.1，授權跟 Cubic 11 完全一樣，
所以問題不在授權，在**小尺寸的光柵化品質**。

點陣字是設計師一格一格手工調的；向量字在 16px 是算出來的，筆畫可能落在
像素邊界上而消失或黏在一起。這支程式把差別畫出來，不用猜。

順帶測一個以前沒討論過的維度：**ILI9341 是 16 位色，不是單色。**
所以字可以做灰階抗鋸齒，而抗鋸齒正好補足向量字在小尺寸的弱點。
代價是字型資料變 2–4 倍（1 bit -> 2 或 4 bit 一像素）。
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import font_preview as FP  # noqa: E402
from font_stroke_test import CONFUSE, DENSE  # noqa: E402

OUT = os.path.join(HERE, "..", "out", "font")
NOTO = "C:\\Windows\\Fonts\\NotoSansTC-VF.ttf"

ROWS = [("筆畫多", DENSE[:9]), ("", DENSE[9:])]
ROWS += [("易混", g) for g in CONFUSE[:5]]
ROWS += [("", g) for g in CONFUSE[5:]]


def quantize(img, levels):
    """把灰階降到 levels 階 —— 模擬「一個像素只有 N bit」的字型資料。"""
    if levels >= 256:
        return img
    step = 255.0 / (levels - 1)
    return img.point(lambda v: int(round(v / step) * step))


def render_noto(size, levels, line_h):
    """levels=2 是純黑白（1 bit），4 是 2 bit，16 是 4 bit。"""
    font = ImageFont.truetype(NOTO, size)
    w, h = 320, line_h * (len(ROWS) + 1) + 8
    gray = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(gray)
    y = 4
    for lab, text in ROWS:
        if lab:
            d.text((4, y), lab, font=font, fill=180)
        d.text((78, y), text, font=font, fill=255)
        y += line_h
    gray = quantize(gray, levels)
    # 上色成跟其他預覽一樣的綠字
    img = Image.new("RGB", (w, h), FP.BG)
    px, gp = img.load(), gray.load()
    for yy in range(h):
        for xx in range(w):
            v = gp[xx, yy]
            if v:
                t = v / 255.0
                px[xx, yy] = tuple(int(FP.BG[i] + (FP.FG[i] - FP.BG[i]) * t)
                                   for i in range(3))
    return img


def label(img, text, scale=3):
    big = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    out = Image.new("RGB", (big.width, big.height + 22), (0, 0, 0))
    out.paste(big, (0, 22))
    ImageDraw.Draw(out).text((4, 4), text, fill=(255, 255, 255))
    return out


def main():
    if not os.path.exists(NOTO):
        print("找不到 %s" % NOTO)
        return 1
    os.makedirs(OUT, exist_ok=True)

    cases = [
        (16, 2, 20, "Noto 16px  黑白 (1 bit/px)"),
        (16, 4, 20, "Noto 16px  4 階灰 (2 bit/px)"),
        (16, 256, 20, "Noto 16px  全灰階"),
        (12, 2, 15, "Noto 12px  黑白 (1 bit/px)"),
    ]
    panels = []
    for size, levels, line_h, name in cases:
        img = render_noto(size, levels, line_h)
        fn = "noto_%dpx_%s.png" % (size, {2: "1bit", 4: "2bit",
                                          256: "gray"}[levels])
        img.save(os.path.join(OUT, fn))
        panels.append(label(img, name))

    gap = 20
    w = sum(p.width for p in panels) + gap * (len(panels) - 1)
    both = Image.new("RGB", (w, max(p.height for p in panels)), (0, 0, 0))
    x = 0
    for p in panels:
        both.paste(p, (x, 0))
        x += p.width + gap
    both.save(os.path.join(OUT, "vector_compare.png"))
    print("寫出 out/font/vector_compare.png（%d 欄）" % len(panels))

    # 覆蓋率：Noto 是完整的 CJK 字型，這點不會輸
    font = ImageFont.truetype(NOTO, 16)
    from fontTools.ttLib import TTFont
    tt = TTFont(NOTO, fontNumber=0, lazy=True)
    cmap = tt.getBestCmap()
    miss = [c for c in DENSE + "".join(CONFUSE) if ord(c) not in cmap]
    total = len(DENSE) + len("".join(CONFUSE))
    print("Noto Sans TC 測試字覆蓋 %d/%d%s"
          % (total - len(miss), total, "  缺：" + "".join(miss) if miss else ""))
    print("字型檔本身 %.1f MB（可變字重，實際只需要一個字重）"
          % (os.path.getsize(NOTO) / 1e6))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
