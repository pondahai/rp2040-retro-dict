#!/usr/bin/env python3
"""D3 的判斷工具：把同一個字典畫面用兩種字型畫出來，直接看。

    python tools/font_preview.py

產生 out/font/ 底下的 PNG：320x240 的實際畫面，一張 11x11、一張 16x16，
再加一張 2 倍放大的並排對照。

**左邊那張是真的**：字模取自本專案生態系已經在用的 Cubic 11（俐方體十一號），
從 rp2040-ili9341-infones 的 font_cjk.h 直接讀出來，不是模擬。

右邊是 Ark Pixel Font（方舟像素字體）16px，**同樣是 SIL OFL 1.1**，
與 Cubic 11 授權條款相同 —— U4 因此不再是風險。它是為 16px 設計的點陣字，
不是向量字縮小，所以這張預覽也是像素精確的。

畫面內容取自**字典裡的真實資料**（ECDICT 的 dictionary 詞條，簡體），
不是手打的樣本 —— 手打成繁體會讓判斷失真。
"""

import os
import re
import sys

from PIL import Image, ImageDraw

import bdf

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
CJK_H = os.path.join(ROOT, "..", "rp2040-ili9341-infones",
                     "software", "infones", "font_cjk.h")
OUT = os.path.join(ROOT, "out", "font")
# Ark Pixel 有 10/12/16px，但**16px 的 CJK 幾乎還沒做**：實測只有 97 個漢字，
# 常用 40 字只涵蓋 9 個。12px 才是完整的（18,299 個漢字，常用字 40/40）。
# 這是量出來的，不是從文件讀的 —— 官網沒有列各尺寸的覆蓋率。
ARK = {
    12: (os.path.join(ROOT, "data", "ark12"),
         "ark-pixel-12px-proportional-zh_cn.bdf",
         "ark-pixel-12px-proportional-latin.bdf"),
    16: (os.path.join(ROOT, "data", "ark"),
         "ark-pixel-16px-proportional-zh_cn.bdf",
         "ark-pixel-16px-proportional-latin.bdf"),
}

W, H = 320, 240
FG = (200, 255, 200)      # 綠字，配合復古定位
BG = (8, 20, 8)
DIM = (90, 130, 90)


# ---------------------------------------------------------------------------
# 讀 Cubic 11 的字模
# ---------------------------------------------------------------------------

def load_cjk():
    """從 font_cjk.h 解出 {codepoint: [每列的 bitmask]}。"""
    if not os.path.exists(CJK_H):
        return None, None, None
    with open(CJK_H, encoding="utf-8", errors="replace") as f:
        text = f.read()

    def const(name):
        # 值可能是十六進位（ASCII_FIRST 是 0x20）。只認十進位的話會抓到
        # "0x20" 裡的 0，整張 ASCII 表偏移 32 格 —— 查 A 會拿到 a。
        m = re.search(r"#define\s+%s\s+(0x[0-9a-fA-F]+|\d+)" % name, text)
        return int(m.group(1), 0) if m else None

    rows = const("CJK_GLYPH_ROWS")
    count = const("CJK_GLYPH_COUNT")
    ascii_rows = const("ASCII_GLYPH_ROWS")
    ascii_first = const("ASCII_FIRST")

    def array(name):
        m = re.search(r"%s\[[^\]]*\]\s*=\s*\{(.*?)\};" % name, text, re.S)
        if not m:
            return []
        body = m.group(1)
        return [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]

    bitmap = array("cjk_bitmap")
    index = array("cjk_index")
    ascii_bm = array("ascii_bitmap")

    glyphs = {}
    for i, cp in enumerate(index[:count]):
        start = i * rows
        glyphs[cp] = bitmap[start:start + rows]
    ascii_glyphs = {}
    for i in range(len(ascii_bm) // ascii_rows):
        ascii_glyphs[ascii_first + i] = ascii_bm[i * ascii_rows:(i + 1) * ascii_rows]
    return glyphs, ascii_glyphs, (rows, ascii_rows)


def draw_cubic(img, glyphs, ascii_glyphs, rows_info, x, y, text, color):
    """用 Cubic 11 的字模畫字。回傳畫完後的 x。"""
    rows, ascii_rows = rows_info
    px = img.load()
    for ch in text:
        cp = ord(ch)
        if cp < 128:
            g = ascii_glyphs.get(cp)
            width, nrows = 8, ascii_rows
        else:
            g = glyphs.get(cp)
            width, nrows = 16, rows
        if g:
            for r in range(min(nrows, len(g))):
                bits = g[r]
                for c in range(width):
                    if bits & (1 << c):
                        xx, yy = x + c, y + r
                        if 0 <= xx < W and 0 <= yy < H:
                            px[xx, yy] = color
        x += 8 if cp < 128 else 12       # Cubic 11 的中文步進約 11-12px
    return x


# ---------------------------------------------------------------------------
# 畫面內容：一個真實的查詢結果
# ---------------------------------------------------------------------------

# 取自 out/DICT 的真實詞條（ECDICT 的 dictionary）。ECDICT 是簡體，
# 手打成繁體會讓字型判斷失真 —— 簡繁的筆畫密度差很多。
ENTRY_EN = "dictionary"
ENTRY_IPA = "[ 'dikʃənəri ]"
ENTRY_LINES = [
    "n. 字典, 词典",
    "[计] 词典",
    "",
    "a reference book containing an",
    "alphabetical list of words",
]
STATUS = "英汉  F1 发音  F2 切换"


def render_cubic():
    glyphs, ascii_glyphs, rows_info = load_cjk()
    if glyphs is None:
        return None
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W - 1, 13], fill=(20, 50, 20))
    draw_cubic(img, glyphs, ascii_glyphs, rows_info, 2, 0, ENTRY_EN, FG)
    draw_cubic(img, glyphs, ascii_glyphs, rows_info, 100, 0, ENTRY_IPA, DIM)
    y = 18
    for line in ENTRY_LINES:
        if line:
            draw_cubic(img, glyphs, ascii_glyphs, rows_info, 2, y, line, FG)
        y += 15
    d.rectangle([0, H - 14, W - 1, H - 1], fill=(20, 50, 20))
    draw_cubic(img, glyphs, ascii_glyphs, rows_info, 2, H - 14, STATUS, DIM)
    return img


def load_ark(size):
    d, zh_name, latin_name = ARK[size]
    zh = os.path.join(d, zh_name)
    latin = os.path.join(d, latin_name)
    if not (os.path.exists(zh) and os.path.exists(latin)):
        return None, None
    return bdf.load(zh), bdf.load(latin)


def draw_ark(img, zh, latin, x, baseline, text, color):
    """Ark Pixel 的拉丁字母與漢字分在不同檔案，各取各的。"""
    px = img.load()
    for ch in text:
        cp = ord(ch)
        font = latin if cp in latin.glyphs else zh
        x += bdf.blit(px, font, x, baseline, cp, color, (W, H))
    return x


def render_ark(size, line_h):
    zh, latin = load_ark(size)
    if zh is None:
        return None
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W - 1, line_h - 3], fill=(20, 50, 20))
    base = line_h - 5
    draw_ark(img, zh, latin, 2, base, ENTRY_EN, FG)
    draw_ark(img, zh, latin, 108, base, ENTRY_IPA, DIM)
    y = base + line_h
    for line in ENTRY_LINES:
        if line:
            draw_ark(img, zh, latin, 2, y, line, FG)
        y += line_h
    d.rectangle([0, H - line_h + 1, W - 1, H - 1], fill=(20, 50, 20))
    draw_ark(img, zh, latin, 2, H - 5, STATUS, DIM)
    return img


def label(img, text, scale=2):
    """放大並加標題，方便在螢幕上看清楚像素。"""
    big = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    out = Image.new("RGB", (big.width, big.height + 22), (0, 0, 0))
    out.paste(big, (0, 22))
    d = ImageDraw.Draw(out)
    d.text((4, 4), text, fill=(255, 255, 255))
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    a = render_cubic()
    b = render_ark(12, 15)
    c = render_ark(16, 20)
    made = []

    if a:
        a.save(os.path.join(OUT, "11x11.png"))
        made.append("11x11.png")
    else:
        print("找不到 %s，跳過 11x11" % CJK_H)
    if b:
        b.save(os.path.join(OUT, "12x12.png"))
        made.append("12x12.png")
    if c:
        c.save(os.path.join(OUT, "16x16.png"))
        made.append("16x16.png")

    panels = []
    if a:
        panels.append(label(a, "11x11  Cubic 11 (OFL 1.1, 生態系已有, 7701 字)"))
    if b:
        panels.append(label(b, "12x12  Ark Pixel (OFL 1.1, 18299 字)"))
    if c:
        panels.append(label(c, "16x16  Ark Pixel (CJK 只有 97 字, 不能用)"))
    if panels:
        gap = 16
        total_w = sum(p.width for p in panels) + gap * (len(panels) - 1)
        both = Image.new("RGB", (total_w, max(p.height for p in panels)),
                         (0, 0, 0))
        x = 0
        for p in panels:
            both.paste(p, (x, 0))
            x += p.width + gap
        both.save(os.path.join(OUT, "compare.png"))
        made.append("compare.png")

    print("寫出 %s" % ", ".join(os.path.join("out/font", m) for m in made))
    print()
    print("320x240 可容納：")
    print("  11x11 -> 約 %d 欄 x %d 列（中文）" % (W // 12, H // 15))
    print("  12x12 -> 約 %d 欄 x %d 列（中文）" % (W // 13, H // 15))
    print("  16x16 -> 約 %d 欄 x %d 列（中文）" % (W // 16, H // 20))


if __name__ == "__main__":
    main()
