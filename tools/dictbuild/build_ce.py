"""CC-CEDICT (`cedict_ts.u8`) -> CE.IDX / CE.DAT。

輸入格式每行是：
    繁體 簡體 [拼音] /釋義1/釋義2/
`#` 開頭是註解。
"""

import re

from . import container as C
from . import pinyin
from .normalize import normalize_ce

LINE = re.compile(r"^(\S+)\s+(\S+)\s+\[([^\]]*)\]\s+/(.*)/\s*$")


def parse(path, stats=None):
    """逐行產生 Entry。壞行不中斷整批轉檔，累計在 stats['bad_lines']。"""
    stats = stats if stats is not None else {}
    unknown = stats.setdefault("unknown_syllables", {})
    with open(path, encoding="utf-8") as f:
        for line in f:
            if not line or line[0] == "#":
                continue
            m = LINE.match(line.rstrip("\n"))
            if not m:
                if line.strip():
                    stats["bad_lines"] = stats.get("bad_lines", 0) + 1
                continue
            trad, simp, py, defs = m.groups()
            key = normalize_ce(simp)
            if not key:
                continue
            senses = [d for d in defs.split("/") if d]
            fields = [
                (C.T_HEADWORD, simp.encode("utf-8")),
                (C.T_PINYIN, py.encode("utf-8")),
                (C.T_DEF_EN, "\n".join(senses).encode("utf-8")),
            ]
            if trad != simp:
                fields.append((C.T_TRAD, trad.encode("utf-8")))
            syl = pinyin.to_ids(py, stats=unknown)
            if syl:
                fields.append((C.T_SYL_ZH, syl))
            yield C.Entry(key=key, fields=fields, rank=rank_ce(simp, senses))


def rank_ce(word, senses):
    """CC-CEDICT 沒有詞頻欄位（FORMAT.md §5），只能用可得的訊號近似：
    短詞優先、釋義多的優先（常用詞通常義項多）。"""
    r = len(word) * 100
    r -= min(len(senses), 9) * 5
    return max(0, min(r, 0xFFFF))


def build(src, idx_path, dat_path, source_tag="CC-CEDICT"):
    stats = {}
    entries = list(parse(src, stats))
    n, size = C.build(entries, idx_path, dat_path,
                      encoding=C.ENC_UTF8, direction=C.DIR_CE,
                      source_tag=source_tag)
    stats["entries"] = n
    stats["dat_bytes"] = size
    return stats
