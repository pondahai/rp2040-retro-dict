"""極簡 BDF 讀取器。給字型預覽用。

BDF 存的就是點陣資料本身，不需要再光柵化一次 —— 跟讀 Cubic 11 的
`font_cjk.h` 是同一種做法，預覽因此是像素精確的，不是「向量字縮小」的近似。

只解析畫字需要的欄位，不是完整實作。
"""


class BDF:
    def __init__(self):
        self.glyphs = {}      # codepoint -> (width, height, xoff, yoff, [rows])
        self.dwidth = {}      # codepoint -> 前進寬度
        self.ascent = 0
        self.descent = 0

    def height(self):
        return self.ascent + self.descent


def load(path):
    f = BDF()
    cp = None
    bbx = None
    dw = None
    rows = None
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if rows is not None:
                if line == "ENDCHAR":
                    if cp is not None and bbx:
                        f.glyphs[cp] = (bbx[0], bbx[1], bbx[2], bbx[3], rows)
                        f.dwidth[cp] = dw if dw is not None else bbx[0]
                    cp, bbx, dw, rows = None, None, None, None
                else:
                    # 每列是十六進位，靠左對齊到位元組邊界
                    rows.append(int(line, 16) if line else 0)
                continue
            if line.startswith("ENCODING "):
                cp = int(line.split()[1])
            elif line.startswith("DWIDTH "):
                dw = int(line.split()[1])
            elif line.startswith("BBX "):
                p = line.split()
                bbx = (int(p[1]), int(p[2]), int(p[3]), int(p[4]))
            elif line.startswith("FONT_ASCENT "):
                f.ascent = int(line.split()[1])
            elif line.startswith("FONT_DESCENT "):
                f.descent = int(line.split()[1])
            elif line == "BITMAP":
                rows = []
    return f


def blit(px, font, x, y, cp, color, clip):
    """把一個字畫到 px 上。y 是**基線**位置。回傳前進寬度。"""
    g = font.glyphs.get(cp)
    if not g:
        return font.dwidth.get(cp, 8)
    w, h, xoff, yoff, rows = g
    # BDF 的每列靠左對齊到位元組邊界，所以要往右移到最高位
    pad = (8 - (w % 8)) % 8
    W, H = clip
    for r, bits in enumerate(rows):
        if bits == 0:
            continue
        bits >>= pad
        for c in range(w):
            if bits & (1 << (w - 1 - c)):
                xx = x + xoff + c
                yy = y - yoff - (h - 1 - r)
                if 0 <= xx < W and 0 <= yy < H:
                    px[xx, yy] = color
    return font.dwidth.get(cp, w)
