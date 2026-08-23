"""把 C 的注音引擎與碼表本身逐筆比對。

    python firmware/compare_ime.py

比法不是抽樣，是**整張碼表 1360 個音節全部走一遍**：從碼表取出查詢鍵、
反推成使用者要按的那串鍵、餵給 C，然後檢查回來的注音符號與候選字串是不是
碼表裡原本那一筆。

這樣驗的是「移植」而不是「資料」：按鍵對照、聲調規則（一聲不打、3/4/6/7）、
二分搜尋的比較函式（池子裡的 key 沒有結尾的 0，長度要另外比）—— 這三段
在 C 裡各寫了一次，寫錯任何一處都會在某些音節上現形。

需要先跑 tools/gen_ime_tables.py 與 firmware/build_ime.bat。
"""

import io
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EXE = os.path.join(HERE, "test_ime.exe")
TABLES = os.path.join(HERE, "ime_tables.h")
BATCH = 40


def load_tables():
    text = io.open(TABLES, encoding="utf-8").read()

    def arr(name):
        m = re.search(re.escape(name) + r"\[(\d+)\]\s*=\s*\{", text)
        start = text.index("{", m.start()) + 1
        end = text.index("};", start)
        return bytes(int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+",
                                                   text[start:end]))

    idx = arr("IME_IDX")
    pool = arr("IME_POOL")
    keymap = {}
    for key, esc in re.findall(r"""\{\s*'(.)',\s*"([^"]+)"\s*\}""", text):
        raw = bytes(int(b, 16) for b in re.findall(r"\\x([0-9A-Fa-f]{2})", esc))
        if raw:
            keymap[key] = raw.decode("utf-8")
    return idx, pool, keymap


TONE_KEY = {"2": "6", "3": "3", "4": "4", "5": "7"}   # 1 聲不用打


def keys_for(query_key, inv):
    """碼表裡的查詢鍵 -> 使用者要按的那串鍵。對不回去就回 None。"""
    out = []
    i = 0
    s = query_key
    while i < len(s):
        ch = s[i]
        if ch.isdigit():
            if ch == "1":
                i += 1
                continue        # 一聲不打
            if ch not in TONE_KEY:
                return None
            out.append(TONE_KEY[ch])
            i += 1
            continue
        if ch not in inv:
            return None
        out.append(inv[ch])
        i += 1
    return "".join(out)


def main():
    if not os.path.exists(EXE):
        print("找不到 %s —— 先跑 firmware/build_ime.bat" % EXE)
        return 1
    idx, pool, keymap = load_tables()
    inv = {v: k for k, v in keymap.items()}
    count = len(idx) // 8

    cases = []
    skipped = 0
    for i in range(count):
        koff, klen, _pad, doff, dlen = struct.unpack_from("<HBBHH", idx, i * 8)
        qkey = pool[koff:koff + klen].decode("utf-8")
        cands = pool[doff:doff + dlen].decode("utf-8")
        keys = keys_for(qkey, inv)
        if keys is None:
            skipped += 1
            continue
        # 一聲不打，所以按鍵串反推回來的注音不含聲調符號
        bopo = "".join(c for c in qkey if not c.isdigit())
        tone = [c for c in qkey if c.isdigit()]
        if tone and tone[0] != "1":
            bopo += {"2": "ˊ", "3": "ˇ", "4": "ˋ", "5": "˙"}[tone[0]]
        cases.append((keys, bopo, cands))

    print("碼表 %d 筆，可反推按鍵的 %d 筆（跳過 %d）"
          % (count, len(cases), skipped))

    fails = 0
    for i in range(0, len(cases), BATCH):
        chunk = cases[i:i + BATCH]
        r = subprocess.run([EXE] + [c[0] for c in chunk], capture_output=True)
        if r.returncode != 0:
            print("C 端失敗：%s" % r.stderr.decode("utf-8", "replace"))
            return 1
        lines = r.stdout.decode("utf-8", "replace").splitlines()
        for (keys, bopo, cands), line in zip(chunk, lines):
            got = line.split("\t")
            g_bopo = got[1] if len(got) > 1 else ""
            g_cand = got[2] if len(got) > 2 else ""
            if g_bopo != bopo or g_cand != cands:
                fails += 1
                if fails <= 10:
                    print("  FAIL  按鍵 %r" % keys)
                    print("        注音 期望 %r 得到 %r" % (bopo, g_bopo))
                    print("        候選 期望 %r 得到 %r"
                          % (cands[:20], g_cand[:20]))

    print()
    print("全部一致" if not fails else "%d 筆不同" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
