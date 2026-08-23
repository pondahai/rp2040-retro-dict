"""CC-CEDICT 的拼音字串 → 音節 id 陣列，含變調。FORMAT.md §4.2。

CC-CEDICT 的寫法有兩個要注意的地方：
  1. ü 寫成 `u:`（例：`lu:4` 是「綠」，不是「路 lu4」）。搞錯會念錯字。
  2. 輕聲寫成 5（例：`de5`），沒有標調的專有名詞則可能完全沒有數字。
"""

import struct

from . import syllable

NEUTRAL = 0


def parse_syllables(text):
    """把 `ni3 hao3` 這種字串拆成 [(音節, 聲調), ...]。

    無法解析的 token 回 (None, 0)，由呼叫端決定是丟棄還是記警告 ——
    這個模組不做政策判斷。
    """
    out = []
    for tok in text.replace("·", " ").split():
        tok = tok.strip().lower()
        if not tok:
            continue
        tone = NEUTRAL
        if tok[-1].isdigit():
            d = int(tok[-1])
            tok = tok[:-1]
            tone = NEUTRAL if d == 5 else d
        tok = tok.replace("u:", "v")          # CC-CEDICT 的 ü
        if not tok.isalpha():
            out.append((None, 0))             # 標點、拉丁字母詞、罕見符號
            continue
        out.append((tok, tone))
    return out


def apply_sandhi(sylls):
    """變調。目前實作三條規則，全部可在不動韌體的情況下修改（§4.2）。

    這三條的正確性尚未經 U3 聽感實驗驗證 —— 實驗結論若不同，改這裡。
    """
    s = [list(x) for x in sylls]

    # 1. 三聲連讀：前一個變二聲。由右往左掃，避免三個以上連續時過度套用。
    for i in range(len(s) - 2, -1, -1):
        if s[i][1] == 3 and s[i + 1][1] == 3:
            s[i][1] = 2

    # 2. 「不」bu4 在四聲前變二聲。
    for i in range(len(s) - 1):
        if s[i][0] == "bu" and s[i][1] == 4 and s[i + 1][1] == 4:
            s[i][1] = 2

    # 3. 「一」yi1：四聲前變二聲，一/二/三聲前變四聲。
    for i in range(len(s) - 1):
        if s[i][0] == "yi" and s[i][1] == 1:
            s[i][1] = 2 if s[i + 1][1] == 4 else 4 if s[i + 1][1] in (1, 2, 3) else 1

    return [tuple(x) for x in s]


def to_ids(text, sandhi=True, stats=None):
    """CC-CEDICT 拼音字串 → 可直接寫進 SYL_ZH 欄位的 bytes（u16 陣列）。"""
    sylls = parse_syllables(text)
    if sandhi:
        sylls = apply_sandhi(sylls)
    ids = []
    for base, tone in sylls:
        if base is None:
            continue
        sid = syllable.syllable_id(base, tone)
        if sid == syllable.UNKNOWN:
            if stats is not None:
                stats[base] = stats.get(base, 0) + 1
            continue
        ids.append(sid)
    return struct.pack("<%dH" % len(ids), *ids)
