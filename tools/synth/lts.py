#!/usr/bin/env python3
"""英文字母 -> 音素（letter-to-sound）。**這一份是規格，韌體有一份 C 的對照。**

ECDICT 只有約三成的詞條附音標，其餘（片語、變化形、專有名詞、學名）沒有。
而且使用者打到一半的任何字母組合本來就不在字典裡 —— 要「邊打邊唸」就得
當場推，不能靠轉檔期算好。所以這套規則兩邊都要有：Python 這份是規格，
`firmware/lts.c` 照抄，`firmware/compare_lts.py` 逐字比對。

規則表只寫在這裡一份，C 那邊的表是 `tools/gen_lts_tables.py` 產生的 ——
兩邊不可能走樣。

**這不是正確的發音器，是「唸得出來」的發音器。** 英文拼寫與發音的對應
充滿例外，規則法必然會唸錯一部分 —— 但對象是本來就完全唸不出來的那七成，
基準線是「逐字母拼」，不是「唸得跟母語者一樣」。1980 年代的電子字典
就是這個水準。

**這不是正確的發音器，是「唸得出來」的發音器。** 英文拼寫與發音的對應
充滿例外，規則法必然會唸錯一部分 —— 但對象是本來就完全唸不出來的那七成，
基準線是「逐字母拼」，不是「唸得跟母語者一樣」。1980 年代的電子字典
就是這個水準。

規則格式沿用經典的上下文改寫式：(左文, 目標, 右文) -> 音素串。
由長到短、由特殊到一般依序比對，第一個match的就用。
"""

VOWELS = set("aeiou")
CONSONANTS = set("bcdfghjklmnpqrstvwxyz")


def _is(chars):
    return lambda c: c in chars


# 上下文的簡寫符號：
#   #  一個以上的母音        :  零個以上的子音
#   ^  一個子音              +  前母音 e/i/y
#   .  濁子音 b/d/v/g/j/l/m/n/r/w/z
#   $  字首/字尾（視位置而定）
CLASS = {
    "#": _is("aeiou"),
    "^": _is(CONSONANTS),
    "+": _is("eiy"),
    ".": _is("bdvgjlmnrwz"),
}

# (左文, 目標, 右文, 音素串)
# 左右文用上面的符號，或直接寫字母；空字串表示不限。
# `$` 在左文表示字首、在右文表示字尾。
RULES = [
    # ---- 字尾的特例先擋，否則會被一般規則吃掉 ----
    ("", "tion", "", "sh ax n"),     # 不限字尾：dictionary 的 tion 也算
    ("", "sion", "", "zh ax n"),
    ("", "ough", "$", "ah f"),
    ("", "augh", "$", "ao"),
    ("", "eigh", "$", "ey"),
    ("", "ight", "$", "ay t"),
    ("", "tch", "", "ch"),
    ("", "dge", "$", "jh"),
    ("", "que", "$", "k"),
    ("", "ing", "$", "ih ng"),
    ("", "ies", "$", "iy z"),
    ("", "ied", "$", "iy d"),
    ("", "ous", "$", "ax s"),
    ("", "ure", "$", "er"),
    ("", "ate", "$", "ey t"),
    ("", "ism", "$", "ih z ax m"),
    ("", "ist", "$", "ih s t"),
    ("", "age", "$", "ih jh"),
    ("", "ary", "$", "eh r iy"),
    ("", "ory", "$", "ao r iy"),
    ("", "ity", "$", "ih t iy"),
    ("", "ogy", "$", "ax jh iy"),
    ("", "ally", "$", "ax l iy"),
    ("", "ely", "$", "l iy"),
    ("", "le", "$", "ax l"),
    ("", "es", "$", "ih z"),
    ("", "ed", "$", "d"),
    ("", "e", "$", ""),          # 字尾不發音的 e

    # ---- 三字母 ----
    ("", "sch", "", "s k"),
    ("", "chr", "", "k r"),
    ("", "thr", "", "th r"),
    ("", "shr", "", "sh r"),
    ("", "air", "", "eh r"),
    ("", "are", "", "eh r"),
    ("", "ear", "", "ih r"),
    ("", "eer", "", "ih r"),
    ("", "oor", "", "uh r"),
    ("", "our", "", "aw er"),
    ("", "oar", "", "ao r"),
    ("", "war", "", "w ao r"),
    ("", "wor", "", "w er"),
    ("", "igh", "", "ay"),
    ("", "ang", "", "ae ng"),
    ("", "ong", "", "ao ng"),
    ("", "ung", "", "ah ng"),
    ("", "ink", "", "ih ng k"),

    # ---- 疊字子音：唸一個音，但拼寫留著（見上方說明）----
    ("", "bb", "", "b"),
    ("", "dd", "", "d"),
    ("", "ff", "", "f"),
    ("", "gg", "", "g"),
    ("", "kk", "", "k"),
    ("", "ll", "", "l"),
    ("", "mm", "", "m"),
    ("", "nn", "", "n"),
    ("", "pp", "", "p"),
    ("", "rr", "", "r"),
    ("", "ss", "", "s"),
    ("", "tt", "", "t"),
    ("", "zz", "", "z"),
    # ---- 兩字母：子音 ----
    ("", "ch", "", "ch"),
    ("", "sh", "", "sh"),
    ("", "th", "", "th"),
    ("", "ph", "", "f"),
    ("", "gh", "", ""),
    ("", "ck", "", "k"),
    ("", "ng", "", "ng"),
    ("", "qu", "", "k w"),
    ("", "wh", "", "w"),
    ("", "wr", "", "r"),
    ("", "kn", "", "n"),
    ("", "gn", "$", "n"),
    ("$", "gn", "", "n"),
    ("", "ps", "", "s"),
    ("", "mb", "$", "m"),
    ("", "mn", "$", "m"),
    ("", "sc", "+", "s"),
    ("", "cc", "+", "k s"),
    ("", "dg", "+", "jh"),

    # ---- 兩字母：母音 ----
    ("", "ai", "", "ey"),
    ("", "ay", "", "ey"),
    ("", "ea", "r", "ih"),
    ("", "ea", "", "iy"),
    ("", "ee", "", "iy"),
    ("", "ei", "", "ey"),
    ("", "ey", "", "ey"),
    ("", "ie", "", "iy"),
    ("", "oa", "", "ow"),
    ("", "oe", "", "ow"),
    ("", "oi", "", "oy"),
    ("", "oy", "", "oy"),
    ("", "oo", "", "uw"),
    ("", "ou", "", "aw"),
    ("", "ow", "$", "ow"),
    ("", "ow", "", "aw"),
    ("", "ue", "", "uw"),
    ("", "ui", "", "uw"),
    ("", "au", "", "ao"),
    ("", "aw", "", "ao"),
    ("", "eu", "", "y uw"),
    ("", "ew", "", "uw"),

    # ---- 母音 + 子音 + 不發音的 e：長母音 ----
    ("", "a", "^e$", "ey"),
    ("", "e", "^e$", "iy"),
    ("", "i", "^e$", "ay"),
    ("", "o", "^e$", "ow"),
    ("", "u", "^e$", "y uw"),

    # ---- 單一子音 ----
    ("", "c", "+", "s"),
    ("", "c", "", "k"),
    ("", "g", "+", "jh"),
    ("", "g", "", "g"),
    ("", "s", "#$", "z"),
    ("", "s", "", "s"),
    ("", "x", "", "k s"),
    ("", "y", "$", "iy"),
    ("$", "y", "", "y"),
    ("", "y", "", "ih"),
    ("", "b", "", "b"),
    ("", "d", "", "d"),
    ("", "f", "", "f"),
    ("", "h", "", "h"),
    ("", "j", "", "jh"),
    ("", "k", "", "k"),
    ("", "l", "", "l"),
    ("", "m", "", "m"),
    ("", "n", "", "n"),
    ("", "p", "", "p"),
    ("", "q", "", "k"),
    ("", "r", "", "r"),
    ("", "t", "", "t"),
    ("", "v", "", "v"),
    ("", "w", "", "w"),
    ("", "z", "", "z"),

    # ---- 單一母音（開音節長、閉音節短）----
    ("", "a", "$", "ax"),        # banana 的字尾 a 是schwa不是 ae
    ("", "o", "$", "ow"),        # hello 的字尾 o
    ("", "a", "^#", "ey"),
    ("", "a", "", "ae"),
    ("", "e", "^#", "iy"),
    ("", "e", "", "eh"),
    ("", "i", "^#", "ay"),
    ("", "i", "", "ih"),
    ("", "o", "^#", "ow"),
    ("", "o", "", "aa"),
    ("", "u", "^#", "uw"),
    ("", "u", "", "ah"),
]


def _match_ctx(pat, text, start, forward):
    """比對上下文。forward=True 表示 text[start:] 往右比，否則往左（反向）。"""
    i = start
    for k, ch in enumerate(pat if forward else pat[::-1]):
        if ch == "$":
            # 字首/字尾：位置要正好到底
            return (i >= len(text)) if forward else (i < 0)
        if ch in CLASS:
            if forward:
                if i >= len(text) or not CLASS[ch](text[i]):
                    return False
                i += 1
            else:
                if i < 0 or not CLASS[ch](text[i]):
                    return False
                i -= 1
        else:
            if forward:
                if i >= len(text) or text[i] != ch:
                    return False
                i += 1
            else:
                if i < 0 or text[i] != ch:
                    return False
                i -= 1
    return True


def word_to_phones(word):
    """一個英文單字 -> [(音素, 重音)]。無法處理的字元直接跳過。"""
    w = "".join(c for c in word.lower() if c.isalpha())
    if not w:
        return []
    out = []
    i = 0
    while i < len(w):
        for left, target, right, phones in RULES:
            if not w.startswith(target, i):
                continue
            if left and not _match_ctx(left, w, i - 1, False):
                continue
            if right and not _match_ctx(right, w, i + len(target), True):
                continue
            out.extend(phones.split())
            i += len(target)
            break
        else:
            i += 1          # 不認得的字母（數字、符號）跳過
    if not out:
        return []
    return _stress(out)


def _stress(phones):
    """把主重音放在第一個母音上。

    真正的英文重音要看詞源與詞尾，規則法猜不準；但**完全沒有重音**會讓
    整個詞唸起來像一串等長的音節，比放錯位置更難聽。第一音節重音是英文
    最常見的模式（名詞尤其如此），拿它當預設。
    """
    from . import phoneme
    done = False
    out = []
    for p in phones:
        if not done and p in phoneme._VOWELS:
            out.append((p, 1))
            done = True
        else:
            out.append((p, 0))
    return out


def to_ids(text):
    """整串文字（可含多個單字）-> 音素 id 陣列的 bytes，接給 SYL_EN。"""
    from . import phoneme
    ids = []
    for word in text.replace("-", " ").replace("/", " ").split():
        for ph, stress in word_to_phones(word):
            pid = phoneme.phoneme_id(ph, stress)
            if pid != phoneme.UNKNOWN:
                ids.append(pid)
    if not ids:
        return b""
    import struct
    return struct.pack("<%dH" % len(ids), *ids)
