#!/usr/bin/env python3
"""用產生出來的 FONT.BIN 畫完整的字典畫面。

    python tools/ui_preview.py

**字模來自 out/DICT/FONT.BIN，不是 TTF** —— 所以這張圖同時驗證了字模檔的
格式、打包、與二分搜尋讀取，而不只是「字型好不好看」。

內容也是真的：詞條從 out/DICT 查出來，前綴候選走 FORMAT.md §8 的
常用詞索引。畫面上每一個字都是韌體到時候會走的同一條路。
"""

import os
import struct
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
ROOT = os.path.join(HERE, "..")

from dictbuild import container as C  # noqa: E402
from dictbuild.normalize import normalize_ec  # noqa: E402
from mkfont import Font  # noqa: E402

DICT_DIR = os.path.join(ROOT, "out", "DICT")
OUT = os.path.join(ROOT, "out", "font")

W, H = 320, 240
BG = (6, 16, 6)
BAR = (18, 46, 18)
# 四階灰對應的顏色。2bit 字模的 0..3 直接查這張表 —— 韌體端也是這樣做。
RAMP = [(6, 16, 6), (95, 140, 95), (160, 210, 160), (215, 255, 215)]
DIM_RAMP = [(6, 16, 6), (55, 80, 55), (95, 130, 95), (140, 180, 140)]
BAR_RAMP = [BAR, (100, 145, 100), (165, 215, 165), (220, 255, 220)]

LINE_H = 19


def draw_text(img, font, x, baseline_top, text, ramp):
    """baseline_top 是字格頂端。回傳畫完的 x。"""
    px = img.load()
    for ch in text:
        g = font.glyph(ord(ch))
        if g is None:
            # 缺字畫空框，看得出來是缺字而不是壞掉
            d = ImageDraw.Draw(img)
            d.rectangle([x + 1, baseline_top + 2, x + 13, baseline_top + 14],
                        outline=ramp[2])
            x += font.cjk_w
            continue
        adv, cell_w, rows = g
        for y, row in enumerate(rows):
            for cx, v in enumerate(row):
                if v:
                    xx, yy = x + cx, baseline_top + y
                    if 0 <= xx < W and 0 <= yy < H:
                        px[xx, yy] = ramp[v]
        x += adv
    return x


def fetch(word):
    d = C.Dictionary(os.path.join(DICT_DIR, "EC.IDX"),
                     os.path.join(DICT_DIR, "EC.DAT"),
                     os.path.join(DICT_DIR, "ECC.IDX"))
    hits = d.lookup(normalize_ec(word))
    if not hits:
        d.close()
        return None, []
    h = hits[0]
    fields = {t: v.decode("utf-8", "replace") for t, v in h.fields.items()
              if t != C.T_FREQ and t != C.T_SYL_EN}
    cands = [k.decode("ascii", "replace")
             for k, _o, _l, _r in d.prefix(normalize_ec(word[:3]), 6)]
    d.close()
    return fields, cands


def wrap(font, text, max_w):
    """按實際字寬斷行，不是按字數 —— 中英混排字寬不同。"""
    lines, cur, w = [], "", 0
    for ch in text:
        g = font.glyph(ord(ch))
        cw = g[0] if g else font.cjk_w
        if w + cw > max_w and cur:
            lines.append(cur)
            cur, w = "", 0
        cur += ch
        w += cw
    if cur:
        lines.append(cur)
    return lines


def render_result(font, word):
    fields, _cands = fetch(word)
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    d.rectangle([0, 0, W - 1, LINE_H - 1], fill=BAR)
    x = draw_text(img, font, 3, 1, fields[C.T_HEADWORD], BAR_RAMP)
    ipa = fields.get(C.T_PHONETIC, "")
    if ipa:
        draw_text(img, font, x + 10, 1, "[" + ipa + "]", BAR_RAMP)

    y = LINE_H + 3
    for para in fields.get(C.T_TRANS_ZH, "").split("\n"):
        for line in wrap(font, para, W - 6):
            draw_text(img, font, 3, y, line, RAMP)
            y += LINE_H
    y += 4
    for para in fields.get(C.T_DEF_EN, "").split("\n"):
        for line in wrap(font, para, W - 6):
            if y > H - LINE_H * 2:
                break
            draw_text(img, font, 3, y, line, DIM_RAMP)
            y += LINE_H

    d.rectangle([0, H - LINE_H, W - 1, H - 1], fill=BAR)
    draw_text(img, font, 3, H - LINE_H + 1, "英漢  F1 發音  F2 切換  F3 放大",
              BAR_RAMP)
    return img


def render_typing(font, typed):
    """邊打邊查：輸入框 + 常用詞優先的候選清單（FORMAT.md §8 模式 B）。"""
    _f, cands = fetch("apple")
    d0 = C.Dictionary(os.path.join(DICT_DIR, "EC.IDX"),
                      os.path.join(DICT_DIR, "EC.DAT"),
                      os.path.join(DICT_DIR, "ECC.IDX"))
    rows = []
    for k, off, ln, _rank in d0.prefix(typed.encode(), 8):
        rec = d0._read_dat(off, ln)
        tr = rec.fields.get(C.T_TRANS_ZH, b"").decode("utf-8", "replace")
        rows.append((k.decode("ascii", "replace"), tr.split("\n")[0]))
    d0.close()

    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W - 1, LINE_H - 1], fill=BAR)
    draw_text(img, font, 3, 1, "查詢： " + typed + "_", BAR_RAMP)

    y = LINE_H + 3
    for i, (word, trans) in enumerate(rows):
        if y > H - LINE_H * 2:
            break
        if i == 0:
            d.rectangle([0, y - 1, W - 1, y + LINE_H - 2], fill=(16, 40, 16))
        x = draw_text(img, font, 3, y, word, RAMP)
        if trans:
            avail = W - 8 - x
            short = wrap(font, trans, avail)[0] if avail > 20 else ""
            draw_text(img, font, x + 8, y, short, DIM_RAMP)
        y += LINE_H

    d.rectangle([0, H - LINE_H, W - 1, H - 1], fill=BAR)
    draw_text(img, font, 3, H - LINE_H + 1, "常用詞優先   ENTER 查詢", BAR_RAMP)
    return img


def main():
    path = os.path.join(DICT_DIR, "FONT.BIN")
    if not os.path.exists(path):
        print("找不到 %s —— 先跑 tools/mkfont.py" % path)
        return 1
    font = Font(path)
    print("字模檔：漢字 %d + 其他 %d，格 %dx%d / %dx%d，%d bit/px"
          % (font.wide_count, font.narrow_count, font.cjk_w, font.cell_h,
             font.narrow_w, font.cell_h, font.bits))

    os.makedirs(OUT, exist_ok=True)
    a = render_result(font, "dictionary")
    a.save(os.path.join(OUT, "ui_result.png"))
    b = render_typing(font, "app")
    b.save(os.path.join(OUT, "ui_typing.png"))

    gap = 12
    both = Image.new("RGB", (W * 2 * 2 + gap, H * 2), (0, 0, 0))
    both.paste(a.resize((W * 2, H * 2), Image.NEAREST), (0, 0))
    both.paste(b.resize((W * 2, H * 2), Image.NEAREST), (W * 2 + gap, 0))
    both.save(os.path.join(OUT, "ui_compare.png"))
    print("寫出 out/font/ui_result.png、ui_typing.png、ui_compare.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
