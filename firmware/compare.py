"""把 C 後台與 Python 參考實作逐筆比對。

Python 那份是規格（`tools/dictbuild/container.py`），C 那份要照它抄。
最容易寫錯的是扇區級二分搜尋 —— Python 版就在那裡踩過一次，症狀是
「查得到某些字、查不到它旁邊的字」，只查幾個常用詞根本測不出來。

所以這裡用**字典裡真正存在的鍵**隨機抽樣幾千筆，兩邊全部要一致：
命中與否、rank、內文欄位、以及**SD 讀取次數**。

    python firmware/compare.py [筆數]

需要先跑過 tools/mkdict.py 產生 out/DICT，並用 firmware/build_pc.bat 編譯。
"""

import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

from dictbuild import container as C  # noqa: E402

EXE = os.path.join(HERE, "test_compare.exe")
DICT_DIR = os.path.join(ROOT, "out", "DICT")

FAILS = []


def run_c(args, base):
    env = dict(os.environ)
    env["DICT_BASE"] = base
    out = subprocess.run([EXE, DICT_DIR] + args, capture_output=True, env=env)
    if out.returncode != 0:
        raise RuntimeError("C 端失敗: %s" % out.stderr.decode("utf-8", "replace"))
    return out.stdout.decode("utf-8", "replace")


def fail(what, detail):
    FAILS.append(what)
    if len(FAILS) <= 10:
        print("  FAIL  %s" % what)
        print("        %s" % detail)


def compare_lookups(base, n):
    idx = os.path.join(DICT_DIR, base + ".IDX")
    dat = os.path.join(DICT_DIR, base + ".DAT")
    d = C.Dictionary(idx, dat)
    total = d.hdr.rec_count
    print("%s：隨機抽 %d 筆比對（共 %d 筆詞條）" % (base, n, total))

    rng = random.Random(1234)
    checked = 0
    for _ in range(n):
        i = rng.randrange(total)
        key24 = d._read_index(i)[0].rstrip(b"\0")
        if not key24:
            continue
        # 一律用十六進位傳鍵。中文的鍵是 UTF-8，走命令列會被字碼頁轉換
        # 弄壞；早期版本因此跳過非 ASCII 的鍵，結果漢英方向一筆都沒測到 ——
        # 「比對通過」但其實半個字典沒驗證。
        arg = key24.hex()
        label = key24.decode("utf-8", "replace")

        d.src.reads = 0
        py_hits = d.lookup(key24)
        py_reads = d.src.reads
        c_out = run_c(["lookuphex", arg], base).strip()
        checked += 1

        lines = [ln for ln in c_out.split("\n") if ln]
        end = [ln for ln in lines if ln.startswith("END")]
        hits = [ln.split("\t") for ln in lines if ln.startswith("HIT")]
        if not end:
            fail("%s: C 端沒有輸出 END" % label, c_out)
            continue
        c_count = int(end[0].split("\t")[1])
        c_reads = int(end[0].split("\t")[2])

        # 同一個鍵可能對應多筆詞條，筆數與順序都要一致
        if c_count != len(py_hits):
            fail("%s: 命中筆數不一致" % label,
                 "C=%d Python=%d" % (c_count, len(py_hits)))
            continue
        if c_reads != py_reads:
            fail("%s: SD 讀取次數不一致" % label,
                 "C=%d Python=%d" % (c_reads, py_reads))
        for k, h in enumerate(py_hits):
            if int(hits[k][1]) != h.rank:
                fail("%s[%d]: rank 不一致" % (label, k),
                     "C=%s Python=%d" % (hits[k][1], h.rank))
            py_hw = h.fields.get(C.T_HEADWORD, b"").decode("utf-8", "replace")
            if hits[k][2] != py_hw.replace("\t", " "):
                fail("%s[%d]: 詞頭不一致" % (label, k),
                     "C=%r Python=%r" % (hits[k][2], py_hw))
    d.close()
    print("  實際比對 %d 筆" % checked)
    return checked


def compare_prefixes(base, prefixes, common_first):
    idx = os.path.join(DICT_DIR, base + ".IDX")
    dat = os.path.join(DICT_DIR, base + ".DAT")
    com = os.path.join(DICT_DIR, base + "C.IDX")
    d = C.Dictionary(idx, dat, com if os.path.exists(com) else None)
    mode = "B 常用詞優先" if common_first else "A 純字母序"
    print("%s：前綴候選比對（%s，%d 組）" % (base, mode, len(prefixes)))
    for p in prefixes:
        py = [k.decode("ascii", "replace")
              for k, _o, _l, _r in d.prefix(p.encode(), 8, 128,
                                            common_first=bool(common_first))]
        c_out = run_c(["prefix", p, "8", "128", str(common_first)], base)
        c = [line.split("\t")[0] for line in c_out.strip().split("\n") if line]
        if py != c:
            fail("前綴 %r（%s）結果不一致" % (p, mode),
                 "C=%s\n        Python=%s" % (c, py))
    d.close()


def main(argv):
    if not os.path.exists(EXE):
        print("找不到 %s —— 先跑 firmware/build_pc.bat" % EXE)
        return 2
    if not os.path.exists(os.path.join(DICT_DIR, "EC.IDX")):
        print("找不到 out/DICT —— 先跑 tools/mkdict.py")
        return 2

    n = int(argv[1]) if len(argv) > 1 else 2000

    print("C 端自我檢查")
    print("  " + run_c(["selftest"], "EC").strip())

    compare_lookups("EC", n)
    compare_lookups("CE", n // 4)
    compare_prefixes("EC", ["a", "ap", "hel", "com", "st", "zz", "q", "xyz"], 1)
    compare_prefixes("EC", ["a", "ap", "hel", "com", "st", "zz", "q", "xyz"], 0)

    print()
    if FAILS:
        print("%d 項不一致" % len(FAILS))
        return 1
    print("C 與 Python 完全一致。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
