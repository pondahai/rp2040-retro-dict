#!/usr/bin/env python3
"""把向量字型轉成 SD 卡上的 16x16 2bit 字模檔。

    python tools/mkfont.py out/DICT

**為什麼是 2 bit 而不是 1 bit**：16px 的純黑白光柵化會讓筆畫掉光
（實測 `微徵徽` 糊成一團），加上四階灰就全部清楚。ILI9341 是 16 位色，
抗鋸齒本來就做得到，只是以前沒把它算進選項裡。代價是資料量翻倍。

**收哪些字**：掃過 `out/DICT` 的 `.DAT`，只收**實際會用到的**字。
實測 14,516 個 —— 比整套 CJK 少一半以上，而且保證不缺字典裡有的字。

格式沿用生態系 `make_cjk_font.py` 的慣例：固定 stride、排序過的碼位索引，
韌體端用二分搜尋，不需要雜湊表也不需要 malloc。
"""

import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")

MAGIC = b"RDICTFNT"
VERSION = 2
CELL_H = 16
CJK_W = 16
# 窄字格要 14 不是 8：8px 放不下 m 與 w，會把 information 畫成 infornation
# （12 還是差一點，m 會被切掉半根柱子；生態系的 Cubic 11 也踩過，它的解法是手繪 8px 的 M/W/w）。
# 每個字另外存自己的前進寬度，所以 i 不會佔掉 12px。
NARROW_W = 14
BITS = 2

# 字重。可變字型的**預設實例是 Thin（100）**，在 16px 會細到筆畫直接消失
# —— 第一版就是這樣，dictionary 的 i 只剩一個點。Medium 在這個尺寸最清楚，
# Bold 會讓「查」裡的「日」糊成一塊。
WEIGHT = "Medium"

# 主字型用 Noto Sans SC（OFL 1.1），它連繁體字也大多有；缺的再從 TC 補。
FONTS = [
    os.path.join(ROOT, "data", "NotoSansSC-VF.otf"),
    "C:\\Windows\\Fonts\\NotoSansTC-VF.ttf",
    # 音標欄位是整套 IPA，加上拉丁擴充與希臘/西里爾（ECDICT 的 schwa 就是
    # 西里爾的）。CJK 字型都沒收這些，要靠拉丁字型補 —— 少了它音標會全部
    # 變成空框。同樣是 OFL 1.1。
    "C:\\Windows\\Fonts\\NotoSans-Regular.ttf",
]

CJK_RANGES = ((0x2E80, 0x9FFF), (0xF900, 0xFAFF),
              (0xFF00, 0xFFEF), (0x3000, 0x303F))


def is_wide(cp):
    return any(lo <= cp <= hi for lo, hi in CJK_RANGES)


# 一定要收的字：注音符號與聲調符號。
#
# 它們**不在字典內文裡**（字典存的是拼音字串，不是注音），所以只靠掃 .DAT
# 會漏 —— 而漏掉不會有任何症狀，直到使用者打到那個音，螢幕上出現一個空框。
# 實測 ㄠ 就是這樣缺的（ㄋㄧ 有、ㄠ 沒有，因為前兩個剛好出現在某些詞條裡）。
ALWAYS = ([cp for cp in range(0x3105, 0x3130)] +      # ㄅ..ㄩ
          [0x02CA, 0x02C7, 0x02CB, 0x02D9] +          # ˊ ˇ ˋ ˙
          [0x3000])                                    # 全形空白


def scan_needed(dict_dir):
    """掃 .DAT 找出實際用到的字，分成寬（漢字）與窄（其餘）兩組。

    窄的那組不是只有 ASCII —— 音標欄位有整套 IPA，還有拉丁擴充、希臘、
    西里爾（ECDICT 的 schwa 就是西里爾的）。實測 2,134 個，收全部只要 100KB，
    沒有理由挑。
    """
    wide, narrow = set(), set()
    for name in ("EC", "CE"):
        path = os.path.join(dict_dir, name + ".DAT")
        if not os.path.exists(path):
            continue
        with open(path, "rb") as f:
            txt = f.read().decode("utf-8", "ignore")
        for ch in txt:
            cp = ord(ch)
            if is_wide(cp):
                wide.add(cp)
            elif 0x20 <= cp < 0x2E80:
                narrow.add(cp)
    for cp in ALWAYS:
        (wide if is_wide(cp) else narrow).add(cp)
    return sorted(wide), sorted(narrow)


def load_fonts():
    out = []
    for p in FONTS:
        if not os.path.exists(p):
            continue
        f = ImageFont.truetype(p, CELL_H)
        try:
            f.set_variation_by_name(WEIGHT)
        except Exception:
            pass          # 不是可變字型就用它本來的字重
        out.append((f, _cmap(p), os.path.basename(p)))
    return out


def _cmap(path):
    from fontTools.ttLib import TTFont
    return set(TTFont(path, lazy=True).getBestCmap())


def rasterize(font, cp, width, baseline):
    """回傳 (前進寬度, width x CELL_H 的四階灰)。"""
    img = Image.new("L", (width, CELL_H), 0)
    d = ImageDraw.Draw(img)
    d.text((0, baseline), chr(cp), font=font, fill=255, anchor="ls")
    px = img.load()
    # 四捨五入，不是截斷。截斷會把 0.5 以下的覆蓋率全部歸零，細筆畫直接消失。
    cells = [[min(3, int(px[x, y] * 3 / 255 + 0.5)) for x in range(width)]
             for y in range(CELL_H)]
    try:
        adv = int(round(font.getlength(chr(cp))))
    except Exception:
        adv = width
    return max(1, min(width, adv)), cells


def pack(cells, width):
    """四階灰 -> 每像素 2 bit，由左而右填進位元組。"""
    out = bytearray()
    for row in cells:
        acc = 0
        n = 0
        for v in row:
            acc |= (v & 3) << (6 - n * 2)
            n += 1
            if n == 4:
                out.append(acc)
                acc = 0
                n = 0
        if n:
            out.append(acc)
    assert len(out) == CELL_H * ((width * BITS + 7) // 8)
    return bytes(out)


def build(dict_dir, out_path):
    fonts = load_fonts()
    if not fonts:
        print("找不到字型檔：\n  " + "\n  ".join(FONTS))
        return 1
    print("字型：" + "、".join(f[2] for f in fonts))

    wide, narrow = scan_needed(dict_dir)
    print("字典實際用到：漢字 %d 個、其他 %d 個（IPA/拉丁擴充/希臘/西里爾）"
          % (len(wide), len(narrow)))

    def pick(cps):
        """決定每個字用哪份字型；都沒有的就跳過（韌體畫空框）。"""
        got, miss = [], []
        for cp in cps:
            for font, cmap, _n in fonts:
                if cp in cmap:
                    got.append((cp, font))
                    break
            else:
                miss.append(cp)
        return got, miss

    wide_picks, wide_miss = pick(wide)
    narrow_picks, narrow_miss = pick(narrow)
    print("可產生 漢字 %d（缺 %d）、其他 %d（缺 %d）"
          % (len(wide_picks), len(wide_miss),
             len(narrow_picks), len(narrow_miss)))

    baseline = CELL_H - 3      # 留 3 px 給下伸部
    wide_stride = CELL_H * ((CJK_W * BITS + 7) // 8)
    narrow_stride = CELL_H * ((NARROW_W * BITS + 7) // 8)

    wide_bits = bytearray()
    for i, (cp, font) in enumerate(wide_picks):
        _adv, cells = rasterize(font, cp, CJK_W, baseline)
        wide_bits += pack(cells, CJK_W)
        if i % 3000 == 0:
            print("  漢字 ...%d/%d" % (i, len(wide_picks)))

    narrow_bits = bytearray()
    narrow_adv = bytearray()
    for cp, font in narrow_picks:
        adv, cells = rasterize(font, cp, NARROW_W, baseline)
        narrow_bits += pack(cells, NARROW_W)
        narrow_adv.append(adv)

    header = bytearray(64)
    header[0:8] = MAGIC
    struct.pack_into("<HBBBBHHII", header, 8,
                     VERSION, CELL_H, CJK_W, NARROW_W, BITS,
                     len(wide_picks), len(narrow_picks),
                     wide_stride, narrow_stride)

    def align(f):
        while f.tell() % 4:
            f.write(b"\0")

    with open(out_path, "wb") as f:
        f.write(header)
        for cp, _font in wide_picks:
            f.write(struct.pack("<H", cp))
        align(f)
        for cp, _font in narrow_picks:
            f.write(struct.pack("<H", cp))
        align(f)
        f.write(narrow_adv)          # 每個窄字的前進寬度，1 byte
        align(f)
        f.write(wide_bits)
        f.write(narrow_bits)

    size = os.path.getsize(out_path)
    print()
    print("寫出 %s" % os.path.normpath(out_path))
    print("  漢字 %.1f KB + 其他 %.1f KB + 索引 %.1f KB = %.1f KB"
          % (len(wide_bits) / 1024, len(narrow_bits) / 1024,
             (len(wide_picks) + len(narrow_picks)) * 2 / 1024, size / 1024))
    print("  每個漢字 %d bytes（16x16），每個窄字 %d bytes（%dx16）"
          % (wide_stride, narrow_stride, NARROW_W))
    return 0


# ---------------------------------------------------------------------------
# 讀回來（給預覽與韌體參考用）
# ---------------------------------------------------------------------------

class Font:
    """讀 FONT.BIN。韌體端要照這個抄 —— 兩張排序過的碼位表，各自二分搜尋。"""

    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        if self.data[0:8] != MAGIC:
            raise ValueError("不是 RDICTFNT 檔")
        (ver, self.cell_h, self.cjk_w, self.narrow_w, self.bits,
         self.wide_count, self.narrow_count,
         self.wide_stride, self.narrow_stride) = struct.unpack_from(
            "<HBBBBHHII", self.data, 8)
        if ver != VERSION:
            raise ValueError("字型檔版本 %d 不支援（本讀取器要 %d）"
                             % (ver, VERSION))

        def align(v):
            return (v + 3) & ~3

        self.wide_idx = 64
        self.narrow_idx = align(self.wide_idx + self.wide_count * 2)
        self.narrow_adv = align(self.narrow_idx + self.narrow_count * 2)
        self.wide_off = align(self.narrow_adv + self.narrow_count)
        self.narrow_off = self.wide_off + self.wide_count * self.wide_stride

    def _find(self, base, count, cp):
        """二分搜尋碼位索引。韌體端做同一件事。"""
        lo, hi = 0, count - 1
        while lo <= hi:
            mid = (lo + hi) // 2
            v = struct.unpack_from("<H", self.data, base + mid * 2)[0]
            if v == cp:
                return mid
            if v < cp:
                lo = mid + 1
            else:
                hi = mid - 1
        return -1

    def glyph(self, cp):
        """回傳 (前進寬度, 格寬, [[0..3]*格寬]*高)，找不到回 None。"""
        i = self._find(self.narrow_idx, self.narrow_count, cp)
        if i >= 0:
            adv = self.data[self.narrow_adv + i]
            off = self.narrow_off + i * self.narrow_stride
            return adv, self.narrow_w, self._unpack(off, self.narrow_w)
        i = self._find(self.wide_idx, self.wide_count, cp)
        if i >= 0:
            off = self.wide_off + i * self.wide_stride
            return self.cjk_w, self.cjk_w, self._unpack(off, self.cjk_w)
        return None

    def _unpack(self, off, width):
        per_row = (width * BITS + 7) // 8
        rows = []
        for y in range(self.cell_h):
            base = off + y * per_row
            row = []
            for x in range(width):
                b = self.data[base + x // 4]
                row.append((b >> (6 - (x % 4) * 2)) & 3)
            rows.append(row)
        return rows


def main(argv):
    dict_dir = argv[1] if len(argv) > 1 else os.path.join(ROOT, "out", "DICT")
    out_path = os.path.join(dict_dir, "FONT.BIN")
    return build(dict_dir, out_path)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
