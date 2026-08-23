#!/usr/bin/env python3
"""從 pico_keyboard_ime_terminal 抽出注音 IME 的碼表與鍵位對照，
產生 firmware/ime_tables.h。

    python tools/gen_ime_tables.py [pico_keyboard_ime_terminal 的路徑]

抽兩樣東西，都是**從正本解析出來的，不是手抄**：

  1. `picotype_data_optimized.h` 的 `zhuyin_idx_raw_opt` / `zhuyin_pool_opt`
     —— 排序過的索引（1360 筆，每筆 8 bytes）與字串池。
  2. `.ino` 裡 `keyToBopomofoChar()` 的 `if (lower_key == 'x') strcpy(buf, "ㄅ")`
     —— 大千鍵盤配列的按鍵對照。

手抄那 40 幾個對照是這個專案最典型的出錯方式（HANDOVER「三、真實資料一定
比規格髒」講的就是這類），所以一律解析。解析不到預期的樣式就報錯，
不會安靜地生出半張表。
"""

import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
OUT = os.path.join(ROOT, "firmware", "ime_tables.h")
DEFAULT_SRC = os.path.join(ROOT, "..", "pico_keyboard_ime_terminal")


def die(msg):
    print("gen_ime_tables: " + msg, file=sys.stderr)
    sys.exit(1)


def parse_byte_array(text, name):
    m = re.search(re.escape(name) + r"\[(\d+)\][^{]*\{", text)
    if not m:
        die("找不到陣列 %s —— 上游的格式變了" % name)
    declared = int(m.group(1))
    start = text.index("{", m.start()) + 1
    depth = 1
    i = start
    while depth:
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
        i += 1
    body = text[start:i - 1]
    vals = [int(v, 0) for v in re.findall(r"0[xX][0-9a-fA-F]+|\d+", body)]
    if len(vals) != declared:
        die("%s 宣告 %d 個元素，實際解析到 %d 個" % (name, declared, len(vals)))
    return vals


def parse_keymap(text):
    """keyToBopomofoChar() 的 if 串 -> {按鍵: 注音符號}。"""
    pat = re.compile(r"lower_key\s*==\s*'(.)'\s*\)\s*\{\s*strcpy\(buf,\s*\"([^\"]+)\"\)")
    pairs = pat.findall(text)
    if len(pairs) < 30:
        die("鍵位對照只解析到 %d 組，上游的寫法可能變了" % len(pairs))
    m = {}
    for key, bopo in pairs:
        if key in m and m[key] != bopo:
            die("按鍵 %r 對到兩個不同的符號：%r / %r" % (key, m[key], bopo))
        m[key] = bopo
    return m


def emit_bytes(name, vals, per_line=16):
    out = ["static const uint8_t %s[%d] = {" % (name, len(vals))]
    for i in range(0, len(vals), per_line):
        chunk = ", ".join("0x%02X" % v for v in vals[i:i + per_line])
        out.append("    " + chunk + ",")
    out.append("};")
    return out


def main(argv):
    src = argv[1] if len(argv) > 1 else DEFAULT_SRC
    data_h = os.path.join(src, "software", "picotype_data_optimized.h")
    ino = os.path.join(src, "software", "pico_keyboard_ime_terminal.ino")
    for p in (data_h, ino):
        if not os.path.exists(p):
            die("找不到 %s —— 把 pico_keyboard_ime_terminal 的路徑當參數傳進來" % p)

    dtext = io.open(data_h, encoding="utf-8", errors="replace").read()
    itext = io.open(ino, encoding="utf-8", errors="replace").read()

    idx = parse_byte_array(dtext, "zhuyin_idx_raw_opt")
    pool = parse_byte_array(dtext, "zhuyin_pool_opt")
    if len(idx) % 8:
        die("索引長度 %d 不是 8 的倍數（每筆 8 bytes）" % len(idx))
    count = len(idx) // 8
    m = re.search(r"zhuyin_idx_count_opt\s*=\s*(\d+)", dtext)
    if m and int(m.group(1)) != count:
        die("索引筆數對不上：上游說 %s，位元組數算出來是 %d"
            % (m.group(1), count))

    keymap = parse_keymap(itext)

    lines = []
    lines.append("/* 產生自 pico_keyboard_ime_terminal —— 不要手改，")
    lines.append(" * 重跑 python tools/gen_ime_tables.py 即可。")
    lines.append(" *")
    lines.append(" * 索引每筆 8 bytes（小端序）：")
    lines.append(" *   u16 key_offset, u8 key_len, u8 padding, u16 data_offset, u16 data_len")
    lines.append(" * key 與候選字串都住在同一個字串池裡。索引按 key 排序，可二分搜尋。")
    lines.append(" */")
    lines.append("")
    lines.append("#define IME_IDX_COUNT %d" % count)
    lines.append("#define IME_REC_SIZE  8")
    lines.append("")
    lines.extend(emit_bytes("IME_IDX", idx))
    lines.append("")
    lines.extend(emit_bytes("IME_POOL", pool))
    lines.append("")
    lines.append("/* 大千配列：按鍵 -> 注音符號（UTF-8）。'\\0' 結尾，未對應的鍵不在表內。 */")
    lines.append("typedef struct { char key; char bopo[4]; } ime_keymap;")
    lines.append("")
    lines.append("static const ime_keymap IME_KEYMAP[%d] = {" % len(keymap))
    for key in sorted(keymap):
        bopo = keymap[key]
        esc = "".join("\\x%02X" % b for b in bopo.encode("utf-8"))
        lines.append('    { \'%s\', "%s" },   /* %s */'
                     % (key if key != "'" else "\\'", esc, bopo))
    lines.append("};")
    lines.append("#define IME_KEYMAP_N %d" % len(keymap))

    io.open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print("寫出 %s" % os.path.normpath(OUT))
    print("  索引 %d 筆（%d bytes）、字串池 %d bytes、鍵位對照 %d 組"
          % (count, len(idx), len(pool), len(keymap)))
    tones = [k for k, v in keymap.items() if v in ("ˊ", "ˇ", "ˋ", "˙")]
    print("  聲調鍵：%s（一聲不打，預設補 1）" % " ".join(sorted(tones)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
