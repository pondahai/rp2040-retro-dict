"""把 C 版 letter-to-sound 與 Python 規格逐字比對。

    python firmware/compare_lts.py [筆數]

規則表只寫在 `tools/synth/lts.py` 一份，C 那邊是 `tools/gen_lts_tables.py`
產生的 —— 但**比對器不是只比表，是比整條路**：上下文比對的方向、`$` 的
邊界、疊字子音、重音放在哪個母音，這些都在程式碼裡，兩邊各寫一次就會走樣。

抽的字不是隨便編的，是**字典裡真正沒有音標的那些詞**（片語、學名、專名、
縮寫）—— 那正是這條路實際會被用到的輸入。另外加上使用者「打到一半」的
前綴，因為邊打邊唸會餵進來的就是那種東西。
"""

import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

from dictbuild import container as C  # noqa: E402
from synth import lts  # noqa: E402

EXE = os.path.join(HERE, "test_lts.exe")
DICT_DIR = os.path.join(ROOT, "out", "DICT")
BATCH = 60          # 一次餵幾個字給 C（命令列長度有限）


def py_ids(text):
    b = lts.to_ids(text)
    return [b[i] | (b[i + 1] << 8) for i in range(0, len(b), 2)]


def sample_words(n):
    """字典裡沒有音標的詞 + 打到一半的前綴。"""
    words = []
    idx = os.path.join(DICT_DIR, "EC.IDX")
    if os.path.exists(idx):
        d = C.Dictionary(idx, os.path.join(DICT_DIR, "EC.DAT"))
        rng = random.Random(99)
        total = d.hdr.rec_count
        while len(words) < n and len(words) < total:
            i = rng.randrange(total)
            key = d._read_index(i)[0].rstrip(b"\0")
            if not key:
                continue
            hits = d.lookup(key)
            if not hits:
                continue
            if hits[0].fields.get(C.T_SYL_EN):
                continue            # 有音標的走另一條路，不歸這裡管
            w = key.decode("ascii", "replace")
            words.append(w)
            if len(w) > 3:          # 順便測「打到一半」
                words.append(w[:len(w) // 2])
        d.close()
    # 幾個手挑的邊界：疊字、字尾 e、字首特例、純數字、單字母
    words += ["apple", "hello", "little", "knife", "psychology", "gnome",
              "queue", "xyzzy", "a", "i", "123", "e-mail", "co-op", ""]
    return words


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    if not os.path.exists(EXE):
        print("找不到 %s —— 先跑 firmware/build_lts.bat" % EXE)
        return 1

    words = [w for w in sample_words(n) if w]
    print("比對 %d 筆（字典裡沒有音標的詞 + 打到一半的前綴 + 手挑邊界）"
          % len(words))

    fails = 0
    for i in range(0, len(words), BATCH):
        chunk = words[i:i + BATCH]
        r = subprocess.run([EXE] + chunk, capture_output=True)
        if r.returncode != 0:
            print("C 端失敗：%s" % r.stderr.decode("utf-8", "replace"))
            return 1
        lines = r.stdout.decode("utf-8", "replace").splitlines()
        if len(lines) != len(chunk):
            print("C 端回了 %d 行，應該是 %d 行" % (len(lines), len(chunk)))
            return 1
        for word, line in zip(chunk, lines):
            got = line.split("\t", 1)
            c_ids = [int(x) for x in got[1].split()] if len(got) > 1 and got[1] else []
            p_ids = py_ids(word)
            if c_ids != p_ids:
                fails += 1
                if fails <= 10:
                    print("  FAIL  %r" % word)
                    print("        Python %s" % p_ids)
                    print("        C      %s" % c_ids)

    print()
    print("完全一致" if not fails else "%d 筆不同" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
