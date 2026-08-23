"""拼音音節 → SYL.PAK 音節 id。FORMAT.md §4.2。

這一整個模組的存在理由是「韌體端不要有拼音解析器」。聲調、變調、輕聲
全部在 PC 上算完，韌體只拿到一串 u16。

音節表**不是硬寫的清單**，而是由聲母 x 韻母規則生成後過濾。這樣做的好處
是表的來源可稽核（規則看得懂），且不需要維護一份幾百行的字面資料。
"""

# 聲母。空字串代表零聲母音節（如 an / er / yi 這類）。
INITIALS = ["", "b", "p", "m", "f", "d", "t", "n", "l",
            "g", "k", "h", "j", "q", "x",
            "zh", "ch", "sh", "r", "z", "c", "s"]

# 韻母，依開口呼分組。
FINALS_A = ["a", "o", "e", "ai", "ei", "ao", "ou", "an", "en", "ang", "eng", "ong", "er"]
FINALS_I = ["i", "ia", "ie", "iao", "iu", "ian", "in", "iang", "ing", "iong"]
FINALS_U = ["u", "ua", "uo", "uai", "ui", "uan", "un", "uang", "ueng"]
FINALS_V = ["v", "ve", "van", "vn"]  # v = ü

# 舌尖/捲舌聲母共用的韻母組。含 "i" —— 那是「知吃詩日資雌思」的空韻，
# 不是 j/q/x 後面那個 i。漏掉它會讓 shi/zhi/zi 這類極常用音節整批消失。
FINALS_SIB = ["a", "e", "ai", "ei", "ao", "ou", "an", "en", "ang", "eng", "ong",
              "i", "u", "ua", "uo", "uai", "ui", "uan", "un", "uang"]
# g/k/h 沒有空韻，也沒有 -o。
FINALS_VELAR = ["a", "e", "ai", "ei", "ao", "ou", "an", "en", "ang", "eng", "ong",
                "u", "ua", "uo", "uai", "ui", "uan", "un", "uang"]

# 哪些聲母能接哪一組韻母。這張表就是整個音節集合的真正定義。
_GROUPS = {
    "":   FINALS_A + FINALS_I + FINALS_U + FINALS_V,
    "b":  ["a", "o", "ai", "ei", "ao", "an", "en", "ang", "eng", "i", "ie", "iao", "ian", "in", "ing", "u"],
    "p":  ["a", "o", "ai", "ei", "ao", "ou", "an", "en", "ang", "eng", "i", "ie", "iao", "ian", "in", "ing", "u"],
    "m":  ["a", "o", "e", "ai", "ei", "ao", "ou", "an", "en", "ang", "eng", "i", "ie", "iao", "iu", "ian", "in", "ing", "u"],
    "f":  ["a", "o", "ei", "ou", "an", "en", "ang", "eng", "u"],
    "d":  ["a", "e", "ai", "ei", "ao", "ou", "an", "en", "ang", "eng", "ong", "i", "ia", "ie", "iao", "iu", "ian", "ing", "u", "uo", "ui", "uan", "un"],
    "t":  ["a", "e", "ai", "ao", "ou", "an", "ang", "eng", "ong", "i", "ie", "iao", "ian", "ing", "u", "uo", "ui", "uan", "un"],
    "n":  ["a", "e", "ai", "ei", "ao", "ou", "an", "en", "ang", "eng", "ong", "i", "ie", "iao", "iu", "ian", "in", "iang", "ing", "u", "uo", "uan", "v", "ve"],
    "l":  ["a", "o", "e", "ai", "ei", "ao", "ou", "an", "ang", "eng", "ong", "i", "ia", "ie", "iao", "iu", "ian", "in", "iang", "ing", "u", "uo", "uan", "un", "v", "ve"],
    "g":  FINALS_VELAR,
    "k":  FINALS_VELAR,
    "h":  FINALS_VELAR,
    "j":  FINALS_I + FINALS_V,
    "q":  FINALS_I + FINALS_V,
    "x":  FINALS_I + FINALS_V,
    "zh": FINALS_SIB,
    "ch": FINALS_SIB,
    "sh": FINALS_SIB,
    "r":  FINALS_SIB,
    "z":  FINALS_SIB,
    "c":  FINALS_SIB,
    "s":  FINALS_SIB,
}

# 零聲母不存在的獨立音節。"ong" 的位置由 weng 佔走，"i/u/v" 已由 _ZERO_SPELL
# 改寫成 yi/wu/yu，這裡只剔除規則生不出正確寫法的殘留。
_EXCLUDE = {"ong"}

# 零聲母音節在拼音書寫上有固定的改寫（i->yi, u->wu, v->yu ...）。
_ZERO_SPELL = {
    "i": "yi", "ia": "ya", "ie": "ye", "iao": "yao", "iu": "you",
    "ian": "yan", "in": "yin", "iang": "yang", "ing": "ying", "iong": "yong",
    "u": "wu", "ua": "wa", "uo": "wo", "uai": "wai", "ui": "wei",
    "uan": "wan", "un": "wen", "uang": "wang", "ueng": "weng",
    "v": "yu", "ve": "yue", "van": "yuan", "vn": "yun",
}


def _spell(initial, final):
    """把 (聲母, 韻母) 拼成標準拼音寫法。"""
    if initial == "":
        return _ZERO_SPELL.get(final, final)
    if initial in ("j", "q", "x") and final.startswith("v"):
        final = "u" + final[1:]      # ju / que / xuan，ü 上兩點省略
    if final == "iu" and initial:
        return initial + "iu"
    if final == "ui" and initial:
        return initial + "ui"
    if final == "un" and initial:
        return initial + "un"
    if initial in ("zh", "ch", "sh", "r", "z", "c", "s") and final == "i":
        return initial + "i"         # zhi / chi / shi / ri / zi / ci / si
    return initial + final


# 規則生不出、但真實存在的音節。目前只有一個：哎「哟」。
_EXTRA = ["yo"]


def _build_table():
    """回傳 (音節清單, 音節 -> (聲母, 韻母))。

    第二個回傳值是給合成器用的：拼音的書寫形式會掩蓋真正的結構
    （`you` 其實是零聲母 + iu、`ju` 的 u 其實是 ü），從拼寫倒推很容易錯。
    生成時本來就知道答案，順手記下來比事後解析可靠。
    """
    seen = list(_EXTRA)
    parts = {"yo": ("", "io")}
    mark = set()
    for ini in INITIALS:
        for fin in _GROUPS[ini]:
            s = _spell(ini, fin)
            if s in _EXCLUDE:
                continue
            if s not in mark:
                mark.add(s)
                seen.append(s)
                parts[s] = (ini, fin)
    seen.sort()
    return seen, parts


SYLLABLES, SYL_PARTS = _build_table()
SYL_INDEX = {s: i for i, s in enumerate(SYLLABLES)}

TONES = 8            # id 的低 3 bits 放聲調
UNKNOWN = 0xFFFF     # 查不到的音節；播放器應跳過並記一次警告


def syllable_id(base, tone):
    """id = 音節序號 * 8 + 聲調。tone: 0=輕聲, 1..4=四聲。"""
    i = SYL_INDEX.get(base)
    if i is None:
        return UNKNOWN
    return i * TONES + (tone if 0 <= tone <= 4 else 0)


def decode_id(sid):
    if sid == UNKNOWN:
        return (None, 0)
    return (SYLLABLES[sid // TONES], sid % TONES)
