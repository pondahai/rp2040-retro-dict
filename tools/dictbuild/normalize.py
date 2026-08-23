"""鍵的正規化。FORMAT.md §3.1。

這是整套格式最容易踩的坑：PC 端轉檔與韌體端查詢必須用**完全一樣**的規則，
不一致會讓查詢靜默失敗（查得到扇區、比不中鍵，看起來就是「這個字典沒有這個字」）。

所以規則寫在這一個檔案裡，並且刻意寫得夠笨、夠好移植到 C —— 不用 regex、
不用 locale、不依賴 Python 的 str.lower() 對非 ASCII 的行為。
"""

_EC_KEEP = set("abcdefghijklmnopqrstuvwxyz0123456789 '.-")

MAX_KEY_BYTES = 255  # .DAT 的 key_len 是 u8


def normalize_ec(word: str) -> bytes:
    """英文鍵：轉小寫 → 只留白名單字元 → 空白壓縮 → 去頭尾空白。

    大小寫只處理 ASCII A-Z。非 ASCII（如 café 的 é）直接丟棄，
    因為韌體端不會有 Unicode 大小寫表。
    """
    out = []
    prev_space = True  # 開頭視為已有空白，達成「去前導空白」
    for ch in word:
        o = ord(ch)
        if 0x41 <= o <= 0x5A:  # A-Z
            ch = chr(o + 32)
            o += 32
        elif o > 0x7F:
            continue
        if ch == " ":
            if prev_space:
                continue
            prev_space = True
            out.append(ch)
            continue
        if ch not in _EC_KEEP:
            continue
        prev_space = False
        out.append(ch)
    while out and out[-1] == " ":
        out.pop()
    return "".join(out).encode("ascii")


def normalize_ce(word: str) -> bytes:
    """中文鍵：UTF-8 原樣，僅去頭尾空白。

    v1 不做繁簡轉換（FORMAT.md §6）—— 查繁體字若該筆只有簡體會查不到。
    """
    return word.strip().encode("utf-8")
