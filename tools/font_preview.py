#!/usr/bin/env python3
"""D3 的判斷工具：把同一個字典畫面用兩種字型畫出來，直接看。

    python tools/font_preview.py

產生 out/font/ 底下的 PNG：320x240 的實際畫面，一張 11x11、一張 16x16，
再加一張 2 倍放大的並排對照。

**左邊那張是真的**：字模取自本專案生態系已經在用的 Cubic 11（俐方體十一號），
從 rp2040-ili9341-infones 的 font_cjk.h 直接讀出來，不是模擬。

右邊是用系統的思源黑體光柵化到 16x16 —— 那只是**體驗預覽**，不是最終字型。
16x16 要用哪套字型、授權如何（U4）還沒解決。
"""

import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
CJK_H = os.path.join(ROOT, "..", "rp2040-ili9341-infones",
                     "software", "infones", "font_cjk.h")
OUT = os.path.join(ROOT, "out", "font")

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

ENTRY_EN = "dictionary"
ENTRY_IPA = "[ 'dikʃənəri ]"
ENTRY_LINES = [
    "n. 字典，詞典；詞彙",
    "[計] 資料字典",
    "",
    "the dictionary of a language",
    "一種語言的詞典",
]
STATUS = "英漢  F1 發音  F2 切換"


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


def _find_font():
    for name in ("msjh.ttc", "msyh.ttc", "mingliu.ttc", "simsun.ttc"):
        p = os.path.join("C:\\Windows\\Fonts", name)
        if os.path.exists(p):
            return p
    return None


def render_16():
    path = _find_font()
    if not path:
        return None
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    # 16px 的中文 + 對應的半形英數
    cjk = ImageFont.truetype(path, 16)
    d.rectangle([0, 0, W - 1, 17], fill=(20, 50, 20))
    d.text((2, 0), ENTRY_EN, font=cjk, fill=FG)
    d.text((110, 1), ENTRY_IPA, font=cjk, fill=DIM)
    y = 22
    for line in ENTRY_LINES:
        if line:
            d.text((2, y), line, font=cjk, fill=FG)
        y += 20
    d.rectangle([0, H - 18, W - 1, H - 1], fill=(20, 50, 20))
    d.text((2, H - 18), STATUS, font=cjk, fill=DIM)
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
    b = render_16()
    made = []

    if a:
        a.save(os.path.join(OUT, "11x11.png"))
        made.append("11x11.png")
    else:
        print("找不到 %s，跳過 11x11" % CJK_H)
    if b:
        b.save(os.path.join(OUT, "16x16.png"))
        made.append("16x16.png")
    else:
        print("找不到系統中文字型，跳過 16x16")

    if a and b:
        la, lb = label(a, "11x11 (Cubic 11, 專案現有)"), label(b, "16x16 (預覽)")
        gap = 16
        both = Image.new("RGB", (la.width + gap + lb.width,
                                 max(la.height, lb.height)), (0, 0, 0))
        both.paste(la, (0, 0))
        both.paste(lb, (la.width + gap, 0))
        both.save(os.path.join(OUT, "compare.png"))
        made.append("compare.png")

    print("寫出 %s" % ", ".join(os.path.join("out/font", m) for m in made))
    print()
    print("320x240 可容納：")
    print("  11x11 -> 約 %d 欄 x %d 列（中文）" % (W // 12, H // 15))
    print("  16x16 -> 約 %d 欄 x %d 列（中文）" % (W // 16, H // 20))


if __name__ == "__main__":
    main()
