"""CC-CEDICT 的拼音字串 → 音節 id 陣列，含變調。FORMAT.md §4.2。

CC-CEDICT 的寫法有兩個要注意的地方：
  1. ü 寫成 `u:`（例：`lu:4` 是「綠」，不是「路 lu4」）。搞錯會念錯字。
  2. 輕聲寫成 5（例：`de5`），沒有標調的專有名詞則可能完全沒有數字。
"""

import struct

from . import syllable

NEUTRAL = 0


def parse_syllables(text, stats=None):
    """把 `ni3 hao3` 這種字串拆成 [(音節, 聲調), ...]。

    CC-CEDICT 的拼音欄位裡混了三種不是漢語音節的東西，都在這裡處理掉，
    以免它們被當成「音節表漏了」而污染錄音清單：

      - `r5` 兒化韻（`yi1 xia4 r5` = 一下儿）—— 併入前一個音節，見下
      - `xx5` 資料本身標示「無拼音」（如疊字符號 々）
      - 大寫單字母 = 外來語裡的字母名（`A quan1 r5` = A圈兒），不是漢語音節

    無法解析的 token 回 (None, 0)，由呼叫端決定丟棄或記警告 ——
    這個模組不做政策判斷。
    """
    out = []
    for raw in text.replace("·", " ").split():
        tok = raw.strip()
        if not tok:
            continue
        # 大寫單字母要在轉小寫**之前**判斷，否則會跟真音節 a / e / o 混淆
        if len(tok) == 1 and tok.isalpha() and tok.isupper():
            _bump(stats, "latin_letters")
            continue
        tok = tok.lower()
        tone = NEUTRAL
        if tok[-1].isdigit():
            d = int(tok[-1])
            tok = tok[:-1]
            tone = NEUTRAL if d == 5 else d
        if tok == "xx":
            _bump(stats, "no_pinyin_marker")
            continue
        if tok == "r" and out:
            # 兒化：不是獨立音節，而是把前一個音節的韻尾捲舌化。真正的合成
            # 要用專門的兒化錄音才自然；v1 近似成後接一個輕聲 er，並記數，
            # 讓 U3 實驗能評估這個近似聽起來可不可以接受。
            _bump(stats, "erhua")
            out.append(("er", NEUTRAL))
            continue
        tok = tok.replace("u:", "v")          # CC-CEDICT 的 ü
        if not tok.isalpha():
            out.append((None, 0))             # 標點、罕見符號
            continue
        out.append((tok, tone))
    return out


def _bump(stats, key):
    if stats is not None:
        stats[key] = stats.get(key, 0) + 1


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


def to_ids(text, sandhi=True, stats=None, unknown=None):
    """CC-CEDICT 拼音字串 → 可直接寫進 SYL_ZH 欄位的 bytes（u16 陣列）。"""
    sylls = parse_syllables(text, stats=stats)
    if sandhi:
        sylls = apply_sandhi(sylls)
    ids = []
    for base, tone in sylls:
        if base is None:
            continue
        sid = syllable.syllable_id(base, tone)
        if sid == syllable.UNKNOWN:
            if unknown is not None:
                unknown[base] = unknown.get(base, 0) + 1
            continue
        ids.append(sid)
    return struct.pack("<%dH" % len(ids), *ids)
