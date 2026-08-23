#!/usr/bin/env python3
"""把 tools/synth/lts.py 的規則表產生成 firmware/lts_tables.h。

    python tools/gen_lts_tables.py

規則只寫在 `lts.py` 一份，C 那邊是產生的 —— 手改 .h 會在下次重跑時被蓋掉，
而且 `firmware/compare_lts.py` 會立刻抓到兩邊不一致。
"""

import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, HERE)

from synth import lts, phoneme  # noqa: E402

OUT = os.path.join(ROOT, "firmware", "lts_tables.h")

MAX_CTX = 4          # 上下文最長幾個符號
MAX_TARGET = 5       # 目標字母最長
MAX_PH = 4           # 一條規則最多產生幾個音素


def main():
    rows = []
    for left, target, right, phones in lts.RULES:
        ph = phones.split()
        for p in ph:
            if p not in phoneme.PH_INDEX:
                sys.exit("音素 %r 不在 PHONEMES 裡（規則 %r）" % (p, target))
        if len(left) > MAX_CTX or len(right) > MAX_CTX:
            sys.exit("上下文太長：%r / %r" % (left, right))
        if len(target) > MAX_TARGET:
            sys.exit("目標太長：%r" % target)
        if len(ph) > MAX_PH:
            sys.exit("音素太多：%r" % phones)
        rows.append((left, target, right, [phoneme.PH_INDEX[p] for p in ph]))

    vowel_ids = sorted(phoneme.PH_INDEX[p] for p in phoneme._VOWELS
                       if p in phoneme.PH_INDEX)
    mask = 0
    for i in vowel_ids:
        mask |= 1 << i

    out = []
    out.append("/* 產生自 tools/synth/lts.py —— 不要手改，改規則請改那一份")
    out.append(" * 再跑 python tools/gen_lts_tables.py。 */")
    out.append("")
    out.append("#define LTS_MAX_PH %d" % MAX_PH)
    out.append("#define LTS_RULES  %d" % len(rows))
    out.append("")
    out.append("typedef struct {")
    out.append("    char left[%d];       /* 反向比對，空字串 = 不限 */" % (MAX_CTX + 1))
    out.append("    char target[%d];" % (MAX_TARGET + 1))
    out.append("    char right[%d];" % (MAX_CTX + 1))
    out.append("    uint8_t ph[LTS_MAX_PH];   /* 音素序號（還沒乘 4 加重音） */")
    out.append("    uint8_t nph;")
    out.append("} lts_rule;")
    out.append("")
    out.append("static const lts_rule LTS_RULE[LTS_RULES] = {")
    for left, target, right, ph in rows:
        body = ", ".join(str(x) for x in ph + [0] * (MAX_PH - len(ph)))
        out.append('    { "%s", "%-5s", "%s", { %s }, %d },'
                   % (left, target, right, body, len(ph)))
    out.append("};")
    out.append("")
    out.append("/* 哪些音素序號是母音 —— 重音要放在第一個母音上。 */")
    out.append("static const uint64_t LTS_VOWEL_MASK = 0x%016XULL;" % mask)
    text = "\n".join(out) + "\n"
    io.open(OUT, "w", encoding="utf-8", newline="\n").write(text)
    print("寫出 %s：%d 條規則，最長目標 %d、最多音素 %d"
          % (os.path.normpath(OUT), len(rows),
             max(len(t) for _l, t, _r, _p in rows),
             max(len(p) for _l, _t, _r, p in rows)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
