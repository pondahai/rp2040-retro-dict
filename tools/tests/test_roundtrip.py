"""不需要真實字典資料的自我驗證。

用合成資料把整條路走完：轉檔 → 寫檔 → 二分搜尋 → 讀回，並且**實際量測**
SD 讀取次數，驗證 FORMAT.md §3.2 宣稱的「約 16 次」不是空話。

    python tools/tests/test_roundtrip.py
"""

import math
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from dictbuild import build_ce, build_ec, container as C, pinyin, syllable
from synth import phoneme
from dictbuild.normalize import normalize_ce, normalize_ec

FAILED = []


def check(cond, what):
    print(("  PASS  " if cond else "  FAIL  ") + what)
    if not cond:
        FAILED.append(what)


def test_normalize():
    print("正規化（FORMAT.md §3.1）")
    check(normalize_ec("  Hello   World  ") == b"hello world", "轉小寫、空白壓縮、去頭尾")
    check(normalize_ec("Café") == b"caf", "非 ASCII 丟棄（韌體端沒有 Unicode 大小寫表）")
    check(normalize_ec("re-do it") == b"re-do it", "連字號保留")
    check(normalize_ec("!!!") == b"", "全部被濾掉時回空鍵，呼叫端要能處理")
    check(normalize_ce(" 你好 ") == "你好".encode("utf-8"), "中文只去頭尾空白")


def test_syllables():
    print("音節與變調（FORMAT.md §4.2）")

    def ids(t):
        b = pinyin.to_ids(t)
        return [syllable.decode_id(i) for i in struct.unpack("<%dH" % (len(b) // 2), b)]

    check(ids("ni3 hao3") == [("ni", 2), ("hao", 3)], "三聲連讀：前字變二聲")
    check(ids("yi1 ge4") == [("yi", 2), ("ge", 4)], "一 + 四聲 -> 二聲")
    check(ids("yi1 tian1") == [("yi", 4), ("tian", 1)], "一 + 一聲 -> 四聲")
    check(ids("bu4 shi4") == [("bu", 2), ("shi", 4)], "不 + 四聲 -> 二聲")
    check(ids("lu:4") == [("lv", 4)], "CC-CEDICT 的 u: 是 ü，不可當成 u")
    check(ids("de5") == [("de", 0)], "5 是輕聲，存成 tone 0")
    check(all(syllable.syllable_id(s, 1) != syllable.UNKNOWN
              for s in ("zhi", "chi", "shi", "ri", "zi", "ci", "si")),
          "空韻音節存在（曾漏掉整批 shi/zhi/zi）")
    check(syllable.decode_id(syllable.syllable_id("hao", 3)) == ("hao", 3), "id 編解碼可逆")


def test_container_roundtrip():
    print("容器讀寫與二分搜尋（FORMAT.md §2–§3）")
    n = 20000
    entries = []
    for i in range(n):
        key = ("w%06d" % i).encode("ascii")
        entries.append(C.Entry(key=key,
                               fields=[(C.T_HEADWORD, key),
                                       (C.T_TRANS_ZH, ("第%d號" % i).encode("utf-8"))],
                               rank=i % 1000))
    with tempfile.TemporaryDirectory() as d:
        idx, dat = os.path.join(d, "T.IDX"), os.path.join(d, "T.DAT")
        C.build(entries, idx, dat, encoding=C.ENC_ASCII_LOWER, direction=C.DIR_EC,
                source_tag="SYNTHETIC")
        check(os.path.getsize(idx) == C.SECTOR + n * C.REC_SIZE, "索引大小 = 檔頭 + N x 32")
        dd = C.Dictionary(idx, dat)
        check(dd.hdr.rec_count == n and dd.hdr.source_tag == "SYNTHETIC", "檔頭讀回一致")

        ok = True
        for i in (0, 1, n // 3, n // 2, n - 1):
            hits = dd.lookup(("w%06d" % i).encode())
            if len(hits) != 1 or hits[0].fields[C.T_TRANS_ZH] != ("第%d號" % i).encode("utf-8"):
                ok = False
        check(ok, "頭、尾、中間都查得到且內文正確")
        check(dd.lookup(b"w999999") == [], "查不到的鍵回空串列，不是例外")

        dd.src.reads = 0
        dd.lookup(b"w010000")
        reads = dd.src.reads
        bound = math.ceil(math.log2(n / C.RECS_PER_SECTOR)) + 3
        check(reads <= bound, "SD 讀取 %d 次（%d 筆，上限 %d）" % (reads, n, bound))
        dd.close()


def test_truncated_keys():
    print("截斷鍵的收斂（FORMAT.md §3.3）")
    # 前 24 bytes 完全相同、只差在尾巴 —— 索引分不出來，必須靠 .DAT 的完整鍵
    base = "abcdefghijklmnopqrstuvwx"
    words = [base + s for s in ("aaa", "bbb", "ccc")]
    entries = [C.Entry(key=w.encode(), fields=[(C.T_HEADWORD, w.encode())])
               for w in words]
    entries += [C.Entry(key=("zz%03d" % i).encode(),
                        fields=[(C.T_HEADWORD, b"z")]) for i in range(50)]
    with tempfile.TemporaryDirectory() as d:
        idx, dat = os.path.join(d, "T.IDX"), os.path.join(d, "T.DAT")
        C.build(entries, idx, dat, encoding=C.ENC_ASCII_LOWER, direction=C.DIR_EC)
        dd = C.Dictionary(idx, dat)
        ok = all(len(dd.lookup(w.encode())) == 1 and
                 dd.lookup(w.encode())[0].key == w.encode() for w in words)
        check(ok, "三個共用同一截斷鍵的詞各自查得到")
        check(dd.lookup((base + "ddd").encode()) == [], "同截斷鍵但不存在的詞不會誤命中")
        dd.close()


def test_prefix():
    print("前綴候選（FORMAT.md §3.4 / §5）")
    entries = [C.Entry(key=b"apple", fields=[(C.T_HEADWORD, b"apple")], rank=10),
               C.Entry(key=b"apparatus", fields=[(C.T_HEADWORD, b"apparatus")], rank=9000),
               C.Entry(key=b"append", fields=[(C.T_HEADWORD, b"append")], rank=500),
               C.Entry(key=b"banana", fields=[(C.T_HEADWORD, b"banana")], rank=1)]
    with tempfile.TemporaryDirectory() as d:
        idx, dat = os.path.join(d, "T.IDX"), os.path.join(d, "T.DAT")
        C.build(entries, idx, dat, encoding=C.ENC_ASCII_LOWER, direction=C.DIR_EC)
        dd = C.Dictionary(idx, dat)
        got = [k for k, _o, _l, _r in dd.prefix(b"ap")]
        check(got == [b"apple", b"append", b"apparatus"], "前綴命中且按 rank 排序")
        check(b"banana" not in got, "前綴不外溢")
        dd.close()


def test_builders():
    print("兩個轉檔器（合成輸入）")
    with tempfile.TemporaryDirectory() as d:
        cedict = os.path.join(d, "cedict.u8")
        with open(cedict, "w", encoding="utf-8") as f:
            f.write("# comment line\n")
            f.write("你好 你好 [ni3 hao3] /hello/hi/\n")
            f.write("中國 中国 [zhong1 guo2] /China/\n")
            f.write("這行是壞的\n")
        stats = build_ce.build(cedict, os.path.join(d, "CE.IDX"), os.path.join(d, "CE.DAT"))
        check(stats["entries"] == 2, "跳過註解，收下 2 筆")
        check(stats.get("bad_lines") == 1, "壞行被計數而非中斷轉檔")
        dd = C.Dictionary(os.path.join(d, "CE.IDX"), os.path.join(d, "CE.DAT"))
        # 索引鍵是**繁體**（注音打出來就是繁體）—— 簡體查不到是預期行為，
        # 不是壞掉。簡體降級成 SIMP 欄位，資料沒有丟。
        check(not dd.lookup(normalize_ce("中国")), "用簡體查不到（鍵是繁體）")
        hit = dd.lookup(normalize_ce("中國"))
        check(len(hit) == 1, "用繁體查得到")
        check(hit and hit[0].fields[C.T_HEADWORD] == "中國".encode("utf-8"),
              "詞頭是繁體")
        check(hit and hit[0].fields[C.T_SIMP] == "中国".encode("utf-8"), "簡體存在 SIMP 欄")
        check(hit and C.T_SYL_ZH in hit[0].fields, "發音音節 id 已在轉檔期算好")
        dd.close()

        csvp = os.path.join(d, "ecdict.csv")
        with open(csvp, "w", encoding="utf-8", newline="") as f:
            f.write("word,phonetic,definition,translation,pos,collins,oxford,tag,bnc,frq,exchange\n")
            f.write("hello,h@lou,greeting,\"int. 喂，你好\",int,3,1,cet4,1200,900,\n")
            f.write("nodef,,,,,,,,,,\n")
            f.write("apple,aepl,fruit,\"n. 蘋果\",n,4,1,cet4,2000,1500,apple/apples\n")
        stats = build_ec.build(csvp, os.path.join(d, "EC.IDX"), os.path.join(d, "EC.DAT"))
        check(stats["entries"] == 2, "無釋義的列被丟棄")
        check(stats.get("dropped_no_sense") == 1, "丟棄原因有記錄")
        dd = C.Dictionary(os.path.join(d, "EC.IDX"), os.path.join(d, "EC.DAT"))
        hit = dd.lookup(b"hello")
        check(len(hit) == 1 and "喂" in hit[0].fields[C.T_TRANS_ZH].decode("utf-8"),
              "中文釋義讀得回來")
        check(hit and hit[0].rank == 900, "rank 取自 frq，且存在索引裡")
        check(hit and len(hit[0].fields[C.T_FREQ]) == 6, "詞頻欄是 3 x u16")
        dd.close()


def test_mismatch_detection():
    print("錯配偵測（FORMAT.md §1）")
    with tempfile.TemporaryDirectory() as d:
        idx, dat = os.path.join(d, "T.IDX"), os.path.join(d, "T.DAT")
        C.build([C.Entry(key=b"a", fields=[(C.T_HEADWORD, b"a")])], idx, dat,
                encoding=C.ENC_ASCII_LOWER, direction=C.DIR_EC)
        with open(dat, "ab") as f:
            f.write(b"\0" * 8)  # 模擬 IDX 與 DAT 版本不一致
        try:
            C.Dictionary(idx, dat)
            check(False, "IDX/DAT 不配對應該報錯")
        except C.FormatError:
            check(True, "IDX/DAT 不配對會報錯，而不是讀出垃圾")


def test_forward_compat():
    print("向前相容（FORMAT.md §4）")
    e = C.Entry(key=b"x", fields=[(C.T_HEADWORD, b"x"), (0x7F, b"future field")])
    r = C._decode_record(C.encode_record(e))
    check(r.key == b"x" and r.fields[C.T_HEADWORD] == b"x", "未知 tag 不影響已知欄位")
    check(r.fields[0x7F] == b"future field", "未知 tag 被保留而非報錯")


def test_common_index():
    print("常用詞索引與 A/B 兩種模式（FORMAT.md §8）")
    # 重現真實資料的形狀：一個常用詞，前面擋著一大票同前綴的罕見詞
    entries = [C.Entry(key=b"hello", fields=[(C.T_HEADWORD, b"hello")], rank=2238)]
    entries += [C.Entry(key=("hel%04d" % i).encode(),
                        fields=[(C.T_HEADWORD, b"x")], rank=0xFFFF)
                for i in range(600)]
    with tempfile.TemporaryDirectory() as d:
        idx = os.path.join(d, "T.IDX")
        dat = os.path.join(d, "T.DAT")
        com = os.path.join(d, "TC.IDX")
        n, size, cn = C.build(entries, idx, dat, encoding=C.ENC_ASCII_LOWER,
                              direction=C.DIR_EC, common_idx_path=com,
                              common_max=100)
        check(cn == 1, "只有 rank 有效的詞進常用詞索引（0xFFFF 不算）")
        check(os.path.getsize(com) == C.SECTOR + cn * C.REC_SIZE,
              "常用詞索引與主索引同格式")

        dd = C.Dictionary(idx, dat, com)
        b = [k for k, _o, _l, _r in dd.prefix(b"hel", 5, common_first=True)]
        check(b and b[0] == b"hello", "模式 B：常用詞排第一（真實資料上 hel -> hello）")
        a = [k for k, _o, _l, _r in dd.prefix(b"hel", 5, common_first=False)]
        check(b"hello" not in a[:1], "模式 A：純字母序，常用詞不會被拉前")
        check(len(a) == 5 and len(b) == 5, "兩種模式都填滿 limit")
        check(len(set(b)) == len(b), "模式 B 不會重複列出同一個詞")
        dd.close()

        # 沒掛常用詞索引時，common_first 不該爆炸
        dd = C.Dictionary(idx, dat)
        got = dd.prefix(b"hel", 5, common_first=True)
        check(len(got) == 5, "沒有常用詞索引時 common_first 靜默退回模式 A")
        dd.close()


def test_common_index_mismatch():
    print("常用詞索引的錯配偵測")
    with tempfile.TemporaryDirectory() as d:
        idx, dat = os.path.join(d, "T.IDX"), os.path.join(d, "T.DAT")
        com = os.path.join(d, "TC.IDX")
        C.build([C.Entry(key=b"a", fields=[(C.T_HEADWORD, b"a")], rank=1)],
                idx, dat, encoding=C.ENC_ASCII_LOWER, direction=C.DIR_EC,
                common_idx_path=com)
        with open(dat, "ab") as f:
            f.write(b"\0" * 4)
        try:
            C.Dictionary(idx, dat, com)
            check(False, "常用詞索引過期應該報錯")
        except C.FormatError:
            check(True, "常用詞索引與 DAT 不同步會報錯（重轉檔時容易只更新一個）")


def test_phonemes():
    print("英文音標 -> 音素（FORMAT.md §4.1 SYL_EN）")
    ph = lambda t: [p for p, _s in phoneme.parse(t)]
    st = lambda t: phoneme.parse(t)

    # ECDICT 的髒資料。每一條都是實際踩過的，不是假想的
    check(ph(chr(0x4D9) + "'b" + chr(0xE6) + "nd" + chr(0x4D9) + "n")
          == ["ax", "b", "ae", "n", "d", "ax", "n"],
          "西里爾 schwa 當成 schwa（比真 IPA schwa 還常見）")
    check(ph("'" + chr(0x254) + ":lb" + chr(0x254) + ":" + chr(94))[-1] == "g",
          "mojibake 的 ^ 還原成 g（Aalborg）")
    check(ph(chr(0xE6) + "d'v" + chr(92) * 4 + ":b" + chr(0x4D9) + "m")
          == ["ae", "d", "v", "er", "b", "ax", "m"],
          "連續反斜線還原成 schwa（ad verbum）")
    check("ea" in ph("'" + chr(0x454) + chr(0x259) + "ri" + chr(0x259) + "l"),
          "西里爾 ye 當成 " + chr(0x25B) + "（actuarial）")
    check("ea" in ph("'" + chr(0x3B5) + chr(0x259) + "ri" + chr(0x259) + "n"),
          "希臘 epsilon 當成 " + chr(0x25B) + "（-arian）")
    check(ph(chr(0x2CA) + chr(0xE6) + "b" + chr(0x4D9) + "tsi")[0] == "ae",
          "U+02CA 是另一種主重音記號（abbotcy）")

    # 這兩條是靜默失敗，沒有測試就會再犯
    got = st("," + chr(0x25A) + "e" + chr(0x26A) + " bi: 'si:")
    check(len(got) > 0,
          "逗號是次重音不是分隔符（誤 split 曾靜默丟掉 15% 的發音）")
    sec = st("," + chr(0xE6) + "b" + chr(0x259))
    check(any(x == 2 for _p, x in sec), "次重音記成 stress=2，與主重音區分")
    check(ph(chr(0x26A) + "g'zem(p)t; eg-")[:2] == ["ih", "g"],
          "分號才是多讀音分隔符，且括號被忽略")

    # 分隔符這一區踩過三次，每次都是靜默失敗，所以每條都留著
    check(ph("'k" + chr(0xE6) + "sl. 'k" + chr(0x251) + ":sl")
          == ["k", "ae", "s", "l"],
          "句點加空白是分隔符（castle 曾被唸成兩次）")
    check(len(ph(".v" + chr(0x252) + "l" + chr(0x4D9) + "'tiliti")) >= 8,
          "單獨的句點是次重音記號，不是分隔符（volatility）")
    check(ph("'b" + chr(0x28C) + "nd, b" + chr(0x28C) + "nt")
          == ["b", "ah", "n", "d"],
          "逗號加空白是分隔符（bund 曾被唸成兩次）")
    check(len(ph(", k" + chr(0xE6) + "lis'" + chr(0x3B8) + "enik")) >= 8,
          "開頭的逗號加空白仍是次重音，切了會整筆沒發音（callisthenic）")
    check(ph(chr(0x251) + ": l" + chr(0x251) + ": 'k" + chr(0x251) + ":t")
          == ["aa", "l", "aa", "k", "aa", "t"],
          "片語的空白是詞界，不是分隔符（a la carte）")

    check(phoneme.decode_id(phoneme.phoneme_id("ae", 1)) == ("ae", 1),
          "音素 id 編解碼可逆")
    # 注意：拉丁字母幾乎都是合法音素，要用真的不可解析的東西測
    check(phoneme.to_ids("中文標點：（）") == b"", "完全無法解析時回空，不是例外")


def test_syl_en_roundtrip():
    print("SYL_EN 存進 .DAT 再讀回")
    with tempfile.TemporaryDirectory() as d:
        csvp = os.path.join(d, "e.csv")
        with open(csvp, "w", encoding="utf-8", newline="") as f:
            hdr = ("word,phonetic,definition,translation,pos,collins,"
                   "oxford,tag,bnc,frq,exchange")
            row = ("hello," + chr(0x4D9) + "'l" + chr(0x259)
                   + "u,greeting,int. hi,int,3,1,cet4,1200,900,")
            f.write(hdr + chr(10) + row + chr(10))
        build_ec.build(csvp, os.path.join(d, "E.IDX"), os.path.join(d, "E.DAT"))
        dd = C.Dictionary(os.path.join(d, "E.IDX"), os.path.join(d, "E.DAT"))
        hit = dd.lookup(b"hello")
        blob = hit[0].fields.get(C.T_SYL_EN, b"")
        check(len(blob) > 0 and len(blob) % 2 == 0, "SYL_EN 是 u16 陣列")
        ids = struct.unpack("<%dH" % (len(blob) // 2), blob)
        phs = [phoneme.decode_id(i)[0] for i in ids]
        check("l" in phs, "音素讀得回來（%s）" % " ".join(phs))
        dd.close()


if __name__ == "__main__":
    for t in (test_normalize, test_syllables, test_container_roundtrip,
              test_truncated_keys, test_prefix, test_builders,
              test_mismatch_detection, test_forward_compat,
              test_common_index, test_common_index_mismatch,
              test_phonemes, test_syl_en_roundtrip):
        t()
    print()
    if FAILED:
        print("%d 項失敗：" % len(FAILED))
        for f in FAILED:
            print("  -", f)
        raise SystemExit(1)
    print("全部通過。")
