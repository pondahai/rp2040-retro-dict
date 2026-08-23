"""英文音標 -> 音素 id。對應 FORMAT.md §4.1 的 `SYL_EN`（tag 0x09）。

跟中文完全平行的推論：**ECDICT 的每筆詞條就附了音標**，所以英文也不需要
letter-to-sound 規則引擎（D2 原本假設要 SAM 或 eSpeak-ng 來做這件事）。
韌體端拿到的一樣是一串 u16，不必解析任何字串。

**這個模組存在的真正理由是資料很髒。** ECDICT 的音標不是嚴格 IPA：

  - schwa 有**兩個字元**：`ә`(U+04D9，西里爾) 出現 15 萬次、`ə`(U+0259，
    真 IPA) 只有 5.8 萬次。照 IPA 標準寫解析器會把常見的那個當未知符號
    丟掉，損失七成的 schwa —— 而且是靜默損失，沒人會發現。
  - 重音記號有兩種：ASCII `'` 與 U+02C8。
  - 長音有 `:` 與 `ː` 兩種。
  - 逗號分隔多個讀音，只取第一個。
"""

# 先把等價字元收斂成一種寫法，後面的比對才不用處理變體。
#
# 這張表**全部**來自真實資料的統計，不是照 IPA 規格寫的。同一個音在
# ECDICT 裡常有好幾種字元，混雜了西里爾、希臘、與轉檔留下的 mojibake：
#   schwa 三種寫法、ɛ 三種寫法、主重音三種寫法。
# 照標準寫解析器會靜默丟掉其中最常見的那些。
_CANON = {
    "ә": "ə",   # 西里爾 schwa -> IPA schwa（15 萬次，比真 IPA 還多）
    "є": "ɛ",   # 西里爾 ye  -> ɛ（actuarial 'xəriəl）
    "ε": "ɛ",   # 希臘 epsilon -> ɛ（-arian 'xəriən）
    "^": "g",         # mojibake（Aalborg -> 'ɔ:lbɔ:^）
    "ˈ": "'",         # 主重音
    "ˊ": "'",         # 主重音的另一種寫法（abbotcy -> xæbətsi）
    "ˌ": ",",         # 次重音
    "ː": ":",         # 長音
    "ɡ": "g",
    "ʰ": "",
    "ɹ": "r", "ɾ": "r", "ʁ": "r",
    "ɫ": "l",
}

# 一到多個反斜線是 mojibake 的 schwa。出現在 ad verbum / adjournal 這類詞的
# ɜ: 位置，直接還原成 schwa，後面的 ":" 會讓它變成 er。
_BACKSLASH_RUN = chr(92)
# 音素表。順序即 id 順序，**一旦有錄音或參數表對應就不能再動**。
# 長度優先比對，所以多字元的要能被找到 —— 見 _LONGEST。
PHONEMES = [
    # 子音
    "p", "b", "t", "d", "k", "g",
    "f", "v", "th", "dh", "s", "z", "sh", "zh", "h",
    "ch", "jh", "m", "n", "ng", "l", "r", "y", "w",
    # 單母音
    "iy", "ih", "eh", "ae", "aa", "ah", "ao", "uh", "uw", "er", "ax",
    # 雙母音
    "ey", "ay", "oy", "ow", "aw", "ia", "ea", "ua",
]
PH_INDEX = {p: i for i, p in enumerate(PHONEMES)}

STRESS_SLOTS = 4          # id 低 2 bits 放重音
UNKNOWN = 0xFFFF

# 音標序列 -> 音素。長的排前面，比對時取最長匹配。
_MAP = {
    # 雙母音（ECDICT 多半寫成兩個字母，而不是單一 IPA 符號）
    "ei": "ey", "eɪ": "ey", "ai": "ay", "aɪ": "ay",
    "ɔi": "oy", "ɔɪ": "oy", "oi": "oy",
    "əu": "ow", "ou": "ow", "oʊ": "ow", "əʊ": "ow",
    "au": "aw", "aʊ": "aw",
    "iə": "ia", "ɪə": "ia",
    "eə": "ea", "ɛə": "ea", "ɛː": "ea",
    "uə": "ua", "ʊə": "ua",
    # 長母音
    "i:": "iy", "iː": "iy", "u:": "uw", "uː": "uw",
    "ɑ:": "aa", "ɑ": "aa", "a:": "aa",
    "ɔ:": "ao", "ɔ": "ao", "ɒ": "ao", "ɒ:": "ao",
    "ə:": "er", "ɜ:": "er", "ɜ": "er", "ɝ": "er", "ɚ": "er",
    # 短母音
    "i": "iy", "ɪ": "ih", "e": "eh", "ɛ": "eh", "æ": "ae",
    "ʌ": "ah", "ʊ": "uh", "u": "uw", "ə": "ax", "a": "ae",
    "o": "ow",
    # 子音
    "tʃ": "ch", "dʒ": "jh", "ʃ": "sh", "ʒ": "zh",
    "θ": "th", "ð": "dh", "ŋ": "ng", "j": "y",
    "p": "p", "b": "b", "t": "t", "d": "d", "k": "k", "g": "g",
    "f": "f", "v": "v", "s": "s", "z": "z", "h": "h",
    "m": "m", "n": "n", "l": "l", "r": "r", "w": "w",
    "x": "k",           # 少數詞用 x 表軟顎擦音，近似成 k
}

_LONGEST = max(len(k) for k in _MAP)

# 這些出現在音標裡但不是音素，直接忽略。
_IGNORE = set(" .-()[]/|:ˑ̯̃͡")


def canonicalize(text):
    out = []
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == _BACKSLASH_RUN:
            while i < len(text) and text[i] == _BACKSLASH_RUN:
                i += 1
            out.append("ə")
            continue
        out.append(_CANON.get(ch, ch))
        i += 1
    return "".join(out)


def parse(text, stats=None):
    """音標字串 -> [(音素, 重音)]。重音：0 無、1 主、2 次。

    多讀音（逗號分隔）只取第一個 —— 字典要念一個音，不是念一串選項。
    """
    text = canonicalize(text.strip())
    # 只有分號分隔多個讀音（-exempt 是 "...; eg-"）。
    # **逗號是次重音記號，不是分隔符** —— 拿它去 split 會把 ",ei bi: 'si:"
    # 這種以次重音開頭的音標切成空字串。實測靜默丟掉 31875 筆（全部發音的
    # 15%），而且沒有任何錯誤訊息。
    if ";" in text:
        text = text.split(";")[0]
    text = text.strip("'\"")
    out = []
    stress = 0
    i = 0
    while i < len(text):
        ch = text[i]
        if ch == "'":
            stress = 1
            i += 1
            continue
        if ch == ",":
            stress = 2
            i += 1
            continue
        if ch in _IGNORE:
            i += 1
            continue
        for ln in range(min(_LONGEST, len(text) - i), 0, -1):
            seg = text[i:i + ln]
            if seg in _MAP:
                ph = _MAP[seg]
                # 重音只掛在母音上；子音吃掉重音記號會讓後面的母音失去它
                if ph in _VOWELS:
                    out.append((ph, stress))
                    stress = 0
                else:
                    out.append((ph, 0))
                i += ln
                break
        else:
            if stats is not None:
                stats[ch] = stats.get(ch, 0) + 1
            i += 1
    return out


_VOWELS = {"iy", "ih", "eh", "ae", "aa", "ah", "ao", "uh", "uw", "er", "ax",
           "ey", "ay", "oy", "ow", "aw", "ia", "ea", "ua"}


def phoneme_id(ph, stress=0):
    i = PH_INDEX.get(ph)
    if i is None:
        return UNKNOWN
    return i * STRESS_SLOTS + (stress if 0 <= stress <= 2 else 0)


def decode_id(pid):
    if pid == UNKNOWN:
        return (None, 0)
    return (PHONEMES[pid // STRESS_SLOTS], pid % STRESS_SLOTS)


def to_ids(text, stats=None):
    """音標字串 -> 可直接寫進 SYL_EN 欄位的 bytes（u16 陣列）。"""
    import struct
    ids = [phoneme_id(p, s) for p, s in parse(text, stats)]
    ids = [i for i in ids if i != UNKNOWN]
    return struct.pack("<%dH" % len(ids), *ids)
