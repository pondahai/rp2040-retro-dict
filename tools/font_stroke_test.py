#!/usr/bin/env python3
"""D3 的關鍵測試：筆畫看不看得清楚。

    python tools/font_stroke_test.py

字典跟遊戲選單不一樣。選單只要認得出「大概是哪個字」，字典有時候要**看筆畫**
—— 查一個不認識的字、或分辨兩個長得很像的字。字型如果把「選」跟「遷」畫成
同一團，那是功能壞掉，不是不好看。

所以這裡不用一般畫面，專挑三類最難的：

  1. 高筆畫字 —— 11px 的先天上限最先在這裡現形
  2. 易混字組 —— 差別只在一兩筆，字典必須分得出來
  3. 部首細節 —— 訁/讠、糹/纟 這類，簡繁對照時要看得出來

判斷標準：**同一組裡的字，你分得出來嗎？**
"""

import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import font_preview as FP  # noqa: E402

OUT = os.path.join(HERE, "..", "out", "font")

# 高筆畫字。鬱 33 畫、齉 36 畫是極端，其餘是字典裡真的會遇到的
DENSE = "鬱籲齉矚讓邊響觀廳灣攪釁鑰驟蘭競藥"
# 易混字組，差別只有一兩筆
CONFUSE = [
    "己已巳", "未末", "日曰", "士土", "戌戍戊",
    "侯候", "睛晴", "辯辨辦", "摺褶", "微徵徽",
]
# 簡繁對照，部首細節
RADICAL = ["語语", "紅红", "鐵铁", "際际", "續续", "醫医"]


def render_rows(draw_fn, rows, w, h, line_h, x0=4, y0=None):
    img = Image.new("RGB", (w, h), FP.BG)
    y = y0 if y0 is not None else line_h
    for label, text in rows:
        draw_fn(img, x0, y, label, FP.DIM)
        draw_fn(img, 78, y, text, FP.FG)
        y += line_h
    return img


def make_cubic_drawer():
    glyphs, ascii_glyphs, rows_info = FP.load_cjk()
    if glyphs is None:
        return None

    def draw(img, x, y, text, color):
        # Cubic 11 的 y 是字格頂端
        FP.draw_cubic(img, glyphs, ascii_glyphs, rows_info, x, y - 12, text, color)
    return draw


def make_ark_drawer(size):
    zh, latin = FP.load_ark(size)
    if zh is None:
        return None

    def draw(img, x, y, text, color):
        FP.draw_ark(img, zh, latin, x, y, text, color)
    return draw


def build(name, draw, line_h, w=320):
    rows = [("筆畫多", DENSE[:9]), ("", DENSE[9:])]
    rows += [("易混", g) for g in CONFUSE[:5]]
    rows += [("", g) for g in CONFUSE[5:]]
    rows += [("簡繁", " ".join(RADICAL[:3])), ("", " ".join(RADICAL[3:]))]
    h = line_h * (len(rows) + 1) + 8
    return render_rows(draw, rows, w, h, line_h)


def label(img, text, scale=3):
    big = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    out = Image.new("RGB", (big.width, big.height + 22), (0, 0, 0))
    out.paste(big, (0, 22))
    ImageDraw.Draw(out).text((4, 4), text, fill=(255, 255, 255))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    panels = []

    d = make_cubic_drawer()
    if d:
        img = build("11", d, 15)
        img.save(os.path.join(OUT, "stroke_11.png"))
        panels.append(label(img, "11x11  Cubic 11"))

    d = make_ark_drawer(12)
    if d:
        img = build("12", d, 16)
        img.save(os.path.join(OUT, "stroke_12.png"))
        panels.append(label(img, "12x12  Ark Pixel"))

    if panels:
        gap = 20
        w = sum(p.width for p in panels) + gap * (len(panels) - 1)
        both = Image.new("RGB", (w, max(p.height for p in panels)), (0, 0, 0))
        x = 0
        for p in panels:
            both.paste(p, (x, 0))
            x += p.width + gap
        both.save(os.path.join(OUT, "stroke_compare.png"))
        print("寫出 out/font/stroke_compare.png（%d 欄，3 倍放大）" % len(panels))

    # 覆蓋率也一起報 —— 高筆畫字常常正是字型漏掉的那些
    glyphs, _a, _r = FP.load_cjk()
    if glyphs:
        miss = [c for c in DENSE if ord(c) not in glyphs]
        print("Cubic 11 高筆畫字覆蓋 %d/%d%s"
              % (len(DENSE) - len(miss), len(DENSE),
                 "  缺：" + "".join(miss) if miss else ""))
    zh, _l = FP.load_ark(12)
    if zh:
        miss = [c for c in DENSE if ord(c) not in zh.glyphs]
        print("Ark 12px 高筆畫字覆蓋 %d/%d%s"
              % (len(DENSE) - len(miss), len(DENSE),
                 "  缺：" + "".join(miss) if miss else ""))


if __name__ == "__main__":
    main()
