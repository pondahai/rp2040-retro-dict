"""`.IDX` / `.DAT` 容器的讀寫。FORMAT.md §2 與 §4 的可執行版本。

reader 部分不只是給測試用 —— 它是查詢後台的**參考實作**。韌體端那份 C 要
逐條對得上這裡的行為，尤其是 §3.2 的扇區級二分搜尋與 §3.3 的截斷鍵收斂。
"""

import os
import struct
from dataclasses import dataclass, field

MAGIC = b"RDICTIDX"
FORMAT_VERSION = 1
SECTOR = 512
REC_SIZE = 32
KEY24 = 24
RECS_PER_SECTOR = SECTOR // REC_SIZE  # 16

ENC_ASCII_LOWER = 0
ENC_UTF8 = 1
DIR_EC = 0
DIR_CE = 1

# .DAT 欄位 tag（FORMAT.md §4.1）
T_HEADWORD = 0x01
T_PHONETIC = 0x02
T_PINYIN = 0x03
T_TRANS_ZH = 0x04
T_DEF_EN = 0x05
T_POS = 0x06
T_EXCHANGE = 0x07
T_FREQ = 0x08
T_SYL_EN = 0x09
T_SYL_ZH = 0x0A
T_TRAD = 0x0B

MAX_SCAN = 64  # §3.3 同鍵鄰居掃描上限


class FormatError(Exception):
    pass


# --------------------------------------------------------------------------
# 寫入
# --------------------------------------------------------------------------

@dataclass
class Entry:
    """轉檔工具產出的中間表示。key 已正規化。"""
    key: bytes
    fields: list = field(default_factory=list)  # [(tag, bytes), ...]
    rank: int = 0


def encode_record(e):
    if not 0 < len(e.key) <= 255:
        raise FormatError("key 長度必須是 1..255: %r" % e.key[:40])
    body = bytearray()
    body.append(len(e.key))
    body += e.key
    for tag, data in e.fields:
        if len(data) > 0xFFFF:
            raise FormatError("欄位 0x%02X 超過 65535 bytes" % tag)
        body += struct.pack("<BH", tag, len(data))
        body += data
    total = len(body) + 2
    if total > 0xFFFF:
        raise FormatError("記錄超過 65535 bytes: %r" % e.key[:40])
    return struct.pack("<H", total) + bytes(body)


def _write_index(path, records, encoding, direction, dat_size,
                 source_tag, build_epoch):
    header = bytearray(SECTOR)
    header[0:8] = MAGIC
    struct.pack_into("<HHIBBHII", header, 8,
                     FORMAT_VERSION, REC_SIZE, len(records),
                     encoding, direction, 0, dat_size, build_epoch)
    header[0x1C:0x1C + 32] = source_tag.encode("ascii")[:32].ljust(32, b"\0")
    with open(path, "wb") as f:
        f.write(header)
        for key24, off, ln, rank in records:
            f.write(key24 + struct.pack("<IHH", off, ln, rank))


def build(entries, idx_path, dat_path, encoding, direction,
          source_tag="", build_epoch=0,
          common_idx_path=None, common_max=20000):
    """把 entries 寫成一組 .IDX/.DAT。

    entries 會被完整排序後再寫，因此需要一次放進記憶體。76 萬筆的索引項是
    ~24MB，PC 上無所謂；.DAT 的內文則是邊排邊寫、不整批留存。

    common_idx_path 會**額外**再寫一份「常用詞索引」（FORMAT.md §8）：
    取 rank 最好的 common_max 筆，一樣按鍵排序。它指向的是**同一個 .DAT**，
    所以只多花索引的空間（2 萬筆 = 640KB），內文一個 byte 都不重複。
    """
    entries = sorted(entries, key=lambda e: e.key[:KEY24].ljust(KEY24, b"\0"))
    index = []
    with open(dat_path, "wb") as dat:
        off = 0
        for e in entries:
            blob = encode_record(e)
            dat.write(blob)
            index.append((e.key[:KEY24].ljust(KEY24, b"\0"), off, len(blob), e.rank))
            off += len(blob)
        dat_size = off

    _write_index(idx_path, index, encoding, direction, dat_size,
                 source_tag, build_epoch)

    common_n = 0
    if common_idx_path:
        ranked = [r for r in index if r[3] < 0xFFFF]
        ranked.sort(key=lambda r: (r[3], r[0]))
        common = sorted(ranked[:common_max], key=lambda r: r[0])
        _write_index(common_idx_path, common, encoding, direction, dat_size,
                     source_tag, build_epoch)
        common_n = len(common)
    return len(index), dat_size, common_n


# --------------------------------------------------------------------------
# 讀取（查詢後台的參考實作）
# --------------------------------------------------------------------------

class SectorSource:
    """韌體端只會提供這一個介面。PC 上用檔案模擬，讓後台完全不需要硬體。

    reads 計數是刻意留的：FORMAT.md §3.2 宣稱 76 萬詞條約 16 次 SD 讀取，
    測試要能真的量到，而不是相信估算。
    """

    def __init__(self, path):
        self._f = open(path, "rb")
        self.reads = 0
        self._cache_n = -1
        self._cache = None

    def sector(self, n):
        """單扇區快取。韌體端本來就要有一個 512B 緩衝區，不算額外成本，
        但省掉的是「二分搜尋收斂後又把同一個扇區讀回來」那 2–3 次 —— 在
        SD 上每次都是實際 I/O，值得。"""
        if n == self._cache_n:
            return self._cache
        self.reads += 1
        self._f.seek(n * SECTOR)
        self._cache = self._f.read(SECTOR).ljust(SECTOR, b"\0")
        self._cache_n = n
        return self._cache

    def close(self):
        self._f.close()


@dataclass
class Header:
    rec_count: int
    encoding: int
    direction: int
    flags: int
    dat_size: int
    source_tag: str


def parse_header(buf):
    if buf[0:8] != MAGIC:
        raise FormatError("不是 RDICTIDX 檔")
    ver, rec_size, rec_count, enc, direction, flags, dat_size, _epoch = \
        struct.unpack_from("<HHIBBHII", buf, 8)
    if ver != FORMAT_VERSION:
        raise FormatError("格式版本 %d 不支援" % ver)
    if rec_size != REC_SIZE:
        raise FormatError("rec_size=%d, 本讀取器只支援 %d" % (rec_size, REC_SIZE))
    tag = buf[0x1C:0x3C].split(b"\0")[0].decode("ascii", "replace")
    return Header(rec_count, enc, direction, flags, dat_size, tag)


@dataclass
class Result:
    key: bytes
    rank: int
    fields: dict  # tag -> bytes（同 tag 只留第一個）


def _decode_record(blob):
    total = struct.unpack_from("<H", blob, 0)[0]
    if total > len(blob):
        raise FormatError("記錄被截斷")
    klen = blob[2]
    key = bytes(blob[3:3 + klen])
    pos = 3 + klen
    fields = {}
    while pos + 3 <= total:
        tag, ln = struct.unpack_from("<BH", blob, pos)
        pos += 3
        if pos + ln > total:
            raise FormatError("欄位 0x%02X 超出記錄邊界" % tag)
        # 未知 tag 一樣收下、不報錯。這是 §4 的向前相容機制。
        fields.setdefault(tag, bytes(blob[pos:pos + ln]))
        pos += ln
    return Result(key, 0, fields)


class IndexView:
    """一個 `.IDX` 檔的搜尋邏輯。

    主索引與常用詞索引（FORMAT.md §8）用的是**完全相同**的檔案格式，
    所以共用這一個類別 —— 韌體端也應該只寫一份，開兩個實例。
    """

    def __init__(self, path):
        self.src = SectorSource(path)
        self.hdr = parse_header(self.src.sector(0))

    def close(self):
        self.src.close()

    @property
    def last_sector(self):
        return (self.hdr.rec_count - 1) // RECS_PER_SECTOR

    def _rec_in(self, sector_buf, i):
        base = i * REC_SIZE
        key24 = bytes(sector_buf[base:base + KEY24])
        off, ln, rank = struct.unpack_from("<IHH", sector_buf, base + KEY24)
        return key24, off, ln, rank

    def _valid_count(self, s):
        """最後一個扇區可能沒填滿 16 筆。"""
        return min(RECS_PER_SECTOR, self.hdr.rec_count - s * RECS_PER_SECTOR)

    def lower_bound(self, key):
        """回傳第一筆 key24 >= key 的全域記錄序號。FORMAT.md §3.2。

        二分搜尋的單位是**扇區**不是記錄：每次 SD 讀取拿回 16 筆，
        在扇區內線性收斂不需要再讀 SD。
        """
        probe = key[:KEY24].ljust(KEY24, b"\0")
        # 找「最後一個首筆 <= probe 的扇區」，不是「第一個首筆 >= probe 的」。
        # 目標通常落在扇區**中間**，用後者會整個跳過該扇區 —— 韌體那份 C
        # 照抄時最容易在這裡寫錯，症狀是「查得到某些字、查不到旁邊的字」。
        lo, hi = 0, self.last_sector  # 邏輯扇區號（0 起算，實體 = +1）
        while lo < hi:
            mid = (lo + hi + 1) // 2
            if self._rec_in(self.src.sector(mid + 1), 0)[0] <= probe:
                lo = mid
            else:
                hi = mid - 1
        buf = self.src.sector(lo + 1)
        n = self._valid_count(lo)
        for i in range(n):
            if self._rec_in(buf, i)[0] >= probe:
                return lo * RECS_PER_SECTOR + i
        # 這個扇區全部小於 probe，答案是下一個扇區的第一筆
        return min((lo + 1) * RECS_PER_SECTOR, self.hdr.rec_count)

    def read(self, i):
        if i >= self.hdr.rec_count:
            return None
        s, j = divmod(i, RECS_PER_SECTOR)
        return self._rec_in(self.src.sector(s + 1), j)

    def scan_prefix(self, key, window):
        """從 lower_bound 起往後掃最多 window 筆前綴相符的索引記錄。"""
        i = self.lower_bound(key)
        pre = key[:KEY24]
        out = []
        while len(out) < window and i < self.hdr.rec_count:
            key24, off, ln, rank = self.read(i)
            if not key24.startswith(pre):
                break
            out.append((key24.rstrip(b"\0"), off, ln, rank))
            i += 1
        return out


class Dictionary:
    """一個查詢方向。可選地掛上常用詞索引（FORMAT.md §8）。

    common_idx 指向的是**同一個 .DAT**，所以掛不掛都不影響內文讀取；
    差別只在 prefix() 的候選來源。這就是為什麼「常用詞優先」可以做成
    純執行期的開關 —— 見 §8 的 A/B。
    """

    def __init__(self, idx_path, dat_path, common_idx_path=None):
        self.main = IndexView(idx_path)
        self.common = IndexView(common_idx_path) if common_idx_path else None
        self._dat = open(dat_path, "rb")
        actual = os.path.getsize(dat_path)
        for view, what in ((self.main, "IDX"), (self.common, "常用詞 IDX")):
            if view is not None and view.hdr.dat_size != actual:
                raise FormatError(
                    "%s 與 DAT 不配對: 檔頭記 %d bytes, 實際 %d bytes"
                    % (what, view.hdr.dat_size, actual))

    def close(self):
        self.main.close()
        if self.common:
            self.common.close()
        self._dat.close()

    # 舊介面：測試與既有呼叫端仍直接用 hdr / src / lower_bound
    @property
    def hdr(self):
        return self.main.hdr

    @property
    def src(self):
        return self.main.src

    def lower_bound(self, key):
        return self.main.lower_bound(key)

    def _read_index(self, i):
        return self.main.read(i)

    def _read_dat(self, off, ln):
        self._dat.seek(off)
        return _decode_record(self._dat.read(ln))

    def lookup(self, key):
        """精確查詢。key 必須已正規化。回傳 Result 串列（同鍵可能多筆）。

        只查主索引 —— 常用詞索引是主索引的子集，查它不會多找到任何東西。
        """
        probe = key[:KEY24].ljust(KEY24, b"\0")
        i = self.main.lower_bound(key)
        hits = []
        for n in range(MAX_SCAN):
            rec = self.main.read(i + n)
            if rec is None or rec[0] != probe:
                break
            r = self._read_dat(rec[1], rec[2])
            if r.key == key:  # 截斷鍵的最終比對用完整鍵（§3.3）
                r.rank = rec[3]
                hits.append(r)
        return hits

    def prefix(self, key, limit=16, window=128, common_first=True):
        """前綴候選。只讀索引不讀 .DAT —— rank 就在索引裡（§5）。

        common_first 就是 FORMAT.md §8 的 A/B 開關，可以在執行期切換：

          True  (B) 先從常用詞索引取，不足再用主索引的字母序補滿。
                    打 `hel` 第一個是 `hello`。
          False (A) 純字母序，就是 1980 年代電子字典的行為。
                    打 `hel` 得到 `helen / helena / held`。

        沒有掛常用詞索引時，common_first 無效果，行為等同 A。

        window 只約束主索引那一段的掃描量：索引按鍵排序，常用詞不見得排在
        前面，掃太少會漏、掃太多只是多幾次 SD 讀取。128 筆 = 8 個扇區。
        """
        out = []
        seen = set()
        if common_first and self.common is not None:
            # 這裡也要掃窗口再排序，不能只取前 limit 筆 —— 常用詞索引一樣是
            # 按鍵排序的，只是 haystack 小很多。只取 limit 筆的話 `hel` 會
            # 得到 hell/helen/helicopter 而漏掉 hello，錯誤性質跟主索引相同。
            cand = self.common.scan_prefix(key, window)
            cand.sort(key=lambda t: (t[3], len(t[0]), t[0]))
            for rec in cand[:limit]:
                if rec[0] not in seen:
                    seen.add(rec[0])
                    out.append(rec)
        if len(out) < limit:
            rest = []
            for r in self.main.scan_prefix(key, window):
                if r[0] in seen:
                    continue
                seen.add(r[0])   # 同一個詞頭可能有多筆（多音字、多詞性），
                rest.append(r)   # 候選清單只該出現一次
            if not out:
                # 純字母序模式（A），或常用詞索引沒命中：依 rank 挑，
                # 讓至少不會被 ap-237 這種編號佔滿整個畫面
                rest.sort(key=lambda t: (t[3], len(t[0]), t[0]))
            out.extend(rest[:limit - len(out)])
        return out[:limit]
