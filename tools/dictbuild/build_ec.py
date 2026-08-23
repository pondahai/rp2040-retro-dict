"""ECDICT (`ecdict.csv`) -> EC.IDX / EC.DAT。

只取本專案用得到的欄位。ECDICT 還有 detail / audio 等欄位，一律不收 ——
.DAT 每多一個 byte 就是 SD 上多一個 byte、查詢時多讀一點。
"""

import csv
import sys

from . import container as C
from .normalize import normalize_ec

csv.field_size_limit(min(sys.maxsize, 2**31 - 1))


def _u16(v):
    try:
        n = int(v)
    except (TypeError, ValueError):
        return 0
    return max(0, min(n, 0xFFFF))


def parse(path, stats=None, min_rank=None):
    stats = stats if stats is not None else {}
    with open(path, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            word = (row.get("word") or "").strip()
            key = normalize_ec(word)
            if not key:
                stats["dropped_empty_key"] = stats.get("dropped_empty_key", 0) + 1
                continue
            trans = (row.get("translation") or "").strip()
            defin = (row.get("definition") or "").strip()
            if not trans and not defin:
                stats["dropped_no_sense"] = stats.get("dropped_no_sense", 0) + 1
                continue
            fields = [(C.T_HEADWORD, word.encode("utf-8"))]
            for tag, val in ((C.T_PHONETIC, row.get("phonetic")),
                             (C.T_TRANS_ZH, trans),
                             (C.T_DEF_EN, defin),
                             (C.T_POS, row.get("pos")),
                             (C.T_EXCHANGE, row.get("exchange"))):
                val = (val or "").strip()
                if val:
                    fields.append((tag, val.encode("utf-8")))
            freq = (_u16(row.get("collins")), _u16(row.get("bnc")), _u16(row.get("frq")))
            if any(freq):
                fields.append((C.T_FREQ,
                               b"".join(x.to_bytes(2, "little") for x in freq)))
            # SYL_EN（tag 0x09）尚未產生 —— 卡在 D2（SAM vs eSpeak-ng）未決。
            # 決定後在這裡補一個 g2p 呼叫即可，索引與其他欄位都不必動。
            r = rank_ec(row)
            if min_rank is not None and r > min_rank:
                continue
            yield C.Entry(key=key, fields=fields, rank=r)


def rank_ec(row):
    """越小越優先（FORMAT.md §5）。

    ECDICT 的 bnc / frq 是「名次」，本身就是越小越常用，可直接當 rank；
    兩者都空的詞（罕見詞、專名）給 0xFFFF 沉到最後。
    """
    for col in ("frq", "bnc"):
        n = _u16(row.get(col))
        if n:
            return n
    return 0xFFFF


def build(src, idx_path, dat_path, source_tag="ECDICT", min_rank=None):
    stats = {}
    entries = list(parse(src, stats, min_rank=min_rank))
    n, size = C.build(entries, idx_path, dat_path,
                      encoding=C.ENC_ASCII_LOWER, direction=C.DIR_EC,
                      source_tag=source_tag)
    stats["entries"] = n
    stats["dat_bytes"] = size
    return stats
