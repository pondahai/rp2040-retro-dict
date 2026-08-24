"""英文音素 -> 波形。與中文**共用同一個合成核心**（`voice.render`）。

這是 D2 的答案。原本的選項是 SAM 或 eSpeak-ng，但兩者都是為了「從拼寫
推出發音」，而 ECDICT 每筆詞條就附了音標（見 `phoneme.py`）。剩下的問題
只是「音素怎麼發出聲音」—— 而 U3 的共振峰合成器已經在做這件事。

所以：**兩個都不要，中英共用一套。** 省掉 eSpeak 的幾百 KB，
U7（flash 空間）也一併放鬆。

中英文的差別只在怎麼「排」共振峰軌跡：
  - 中文：聲母 + 韻母，聲調是基頻軌跡
  - 英文：音素序列，重音是時長與音高的組合
真正發聲的機制完全相同。
"""

from . import voice

# 母音的共振峰 (F1, F2, F3)。用 phoneme.py 的音素名稱。
VOWELS = {
    "iy": (300, 2300, 3000),   # beat
    "ih": (400, 1900, 2550),   # bit
    "eh": (550, 1800, 2500),   # bet
    "ae": (700, 1700, 2400),   # bat
    "aa": (750, 1100, 2500),   # father
    "ah": (680, 1450, 2500),   # but（ʌ 偏央，F2 要比 ɑ 高，否則與 father 混）
    "ao": (570,  850, 2500),   # bought
    "uh": (450, 1100, 2350),   # book
    "uw": (320,  900, 2200),   # boot
    "er": (490, 1350, 1500),   # bird，F3 壓低是捲舌
    "ax": (500, 1400, 2500),   # about（schwa）
}

# 雙母音 = 兩個目標之間滑行
DIPHTHONGS = {
    "ey": ("eh", "iy"), "ay": ("aa", "iy"), "oy": ("ao", "iy"),
    "ow": ("ao", "uw"), "aw": ("aa", "uw"),
    "ia": ("iy", "ax"), "ea": ("eh", "ax"), "ua": ("uw", "ax"),
}

# 子音。(型別, 噪音中心 Hz, 是否送氣/清音)
CONSONANTS = {
    "p": ("stop", 800, True), "b": ("stop", 800, False),
    "t": ("stop", 1800, True), "d": ("stop", 1800, False),
    "k": ("stop", 1500, True), "g": ("stop", 1500, False),
    "f": ("fricative", 1400, True), "v": ("fricative", 1400, False),
    "th": ("fricative", 1600, True), "dh": ("fricative", 1600, False),
    "s": ("fricative", 5000, True), "z": ("fricative", 5000, False),
    "sh": ("fricative", 2400, True), "zh": ("fricative", 2400, False),
    "h": ("fricative", 1000, True),
    "ch": ("affricate", 2400, True), "jh": ("affricate", 2400, False),
    "m": ("nasal", 0, False), "n": ("nasal", 0, False), "ng": ("nasal", 0, False),
    "l": ("lateral", 0, False), "r": ("approx", 0, False),
    "y": ("glide", 0, False), "w": ("glide", 0, False),
}

# 鼻音／邊音／滑音的共振峰
_ARTIC = {
    "m": (250, 1100, 2300), "n": (250, 1700, 2600), "ng": (250, 900, 2300),
    "l": (350, 1100, 2600), "r": (400, 1300, 1500),
    "y": (300, 2200, 3000), "w": (330,  800, 2200),
}

# 長母音。英文的 beat/bit、boot/book、father/but 主要靠**長度**分辨，
# 只靠共振峰分不開 —— 實測 but 與 father 的 F1 只差 90Hz、F2 幾乎相同。
LONG_VOWELS = {"iy", "aa", "ao", "uw", "er"}
LONG_FACTOR = 1.5

# 時長（毫秒）。重音會再乘上係數。
DUR_VOWEL = 120
DUR_DIPHTHONG = 170
DUR_STOP_CLOSURE = 45
DUR_BURST = 8
DUR_ASPIRATION = 40
DUR_FRICATIVE = 90
DUR_NASAL = 65
DUR_GLIDE = 55

STRESS_DUR = {0: 0.80, 1: 1.35, 2: 1.05}   # 重音把母音拉長
STRESS_F0 = {0: 0.92, 1: 1.18, 2: 1.03}    # 也把音高抬高

BASE_LEVEL = 1.0     # 相對 voice.BASE_F0

# 句末降調：整個詞的最後一個母音比第一個低這麼多（比例）。英文陳述句的
# 音高整體下滑，少了它每個字都像獨立念出來的。名字取出來是因為韌體端要
# 用同一個值（gen_tables.py -> SYN_EN_DECL_Q8）。
DECLINATION = 0.22


def _seg(kind, n, formants, bw, voiced, f0=None):
    return {"kind": kind, "n": n, "formants": formants, "bw": bw,
            "voiced": voiced, "f0": f0}


def _decl(seen_v, n_vowels):
    """第 seen_v 個母音（0 起算）的降調倍率。"""
    if n_vowels <= 1:
        return 1.0
    return 1.0 - DECLINATION * (seen_v / (n_vowels - 1))


def plan(phones, rate=1.0):
    """[(音素, 重音)] -> 分段清單。每段是等長的共振峰目標。

    句末降調在這裡加：英文的陳述句音高整體下滑，少了它會聽起來像
    每個字都是獨立念出來的。
    """
    segs = []
    n_vowels = sum(1 for p, _s in phones if p in VOWELS or p in DIPHTHONGS)
    seen_v = 0
    for ph, stress in phones:
        if ph in DIPHTHONGS:
            a, b = DIPHTHONGS[ph]
            dur = DUR_DIPHTHONG * STRESS_DUR.get(stress, 1.0) * rate
            decl = _decl(seen_v, n_vowels)
            segs.append(_seg("vowel", voice._ms(dur),
                             [VOWELS[a], VOWELS[b]], voice.VOWEL_BW, True,
                             BASE_LEVEL * STRESS_F0.get(stress, 1.0) * decl))
            seen_v += 1
        elif ph in VOWELS:
            dur = DUR_VOWEL * STRESS_DUR.get(stress, 1.0) * rate
            if ph in LONG_VOWELS:
                dur *= LONG_FACTOR
            decl = _decl(seen_v, n_vowels)
            segs.append(_seg("vowel", voice._ms(dur), [VOWELS[ph]],
                             voice.VOWEL_BW, True,
                             BASE_LEVEL * STRESS_F0.get(stress, 1.0) * decl))
            seen_v += 1
        elif ph in CONSONANTS:
            kind, nf, unvoiced = CONSONANTS[ph]
            if kind == "stop":
                segs.append(_seg("silence", voice._ms(DUR_STOP_CLOSURE * rate),
                                 [(nf, max(nf, 1200), 2600)], voice.NOISE_BW, False))
                segs.append(_seg("noise", voice._ms(DUR_BURST),
                                 [(nf, max(nf, 1200), 2600)], voice.NOISE_BW, False))
                if unvoiced:
                    segs.append(_seg("noise", voice._ms(DUR_ASPIRATION * rate),
                                     [(nf, max(nf, 1200), 2600)],
                                     voice.NOISE_BW, False))
            elif kind in ("fricative", "affricate"):
                if kind == "affricate":
                    segs.append(_seg("silence", voice._ms(25 * rate),
                                     [(nf, nf, 2600)], voice.NOISE_BW, False))
                segs.append(_seg("noise", voice._ms(DUR_FRICATIVE * rate),
                                 [(nf, max(nf, 1200), 2600)], voice.NOISE_BW, False))
            elif kind == "nasal":
                segs.append(_seg("vowel", voice._ms(DUR_NASAL * rate),
                                 [_ARTIC[ph]], voice.VOWEL_BW, True, BASE_LEVEL))
            else:   # lateral / approx / glide
                segs.append(_seg("vowel", voice._ms(DUR_GLIDE * rate),
                                 [_ARTIC[ph]], voice.VOWEL_BW, True, BASE_LEVEL))
    return segs


def synth(phones, rate=1.0):
    """[(音素, 重音)] -> 取樣點串列。"""
    segs = [s for s in plan(phones, rate) if s["n"] > 0]
    if not segs:
        return []
    total = sum(s["n"] for s in segs)

    # 三條軌跡：基頻、共振峰、頻寬。噪音段的 f0 沿用前後的值即可。
    f0_track, track, bw_track, voiced_flag = [], [], [], []
    last_f0 = BASE_LEVEL
    for s in segs:
        if s["f0"]:
            last_f0 = s["f0"]
        pts = s["formants"]
        seg_track = _interp(pts, s["n"])
        for i in range(s["n"]):
            f0_track.append(voice.BASE_F0 * last_f0)
            track.append(seg_track[i])
            bw_track.append(s["bw"])
            voiced_flag.append(s["kind"] == "vowel")

    # 共振峰在段與段之間也要滑行，否則每個音素之間會有喀噠聲
    track = _smooth(track, voice._ms(18))

    voiced = voice._voiced_source(total, f0_track)
    src = []
    for i in range(total):
        if voiced_flag[i]:
            src.append(voiced[i])
        elif bw_track[i] is voice.NOISE_BW and track[i][0] > 0:
            src.append(voice._noise(1, 0.4)[0])
        else:
            src.append(0.0)
    # 靜音段（塞音成阻）要真的靜音
    idx = 0
    for s in segs:
        if s["kind"] == "silence":
            for i in range(idx, idx + s["n"]):
                src[i] = 0.0
        idx += s["n"]

    # pre_len 傳 0：英文的子音已經是獨立的段，響度由整段一起正規化
    return voice.render(src, track, bw_track, 0)


def _interp(pts, n):
    if len(pts) == 1:
        return [pts[0]] * n
    out = []
    seg = len(pts) - 1
    for i in range(n):
        p = i / max(1, n - 1) * seg
        k = min(int(p), seg - 1)
        t = p - k
        t = t * t * (3 - 2 * t)
        out.append(tuple(voice._lerp(pts[k][j], pts[k + 1][j], t) for j in range(3)))
    return out


def _smooth(track, win):
    """對共振峰軌跡做移動平均，把音素邊界的跳變抹平。"""
    if win < 2:
        return track
    n = len(track)
    out = []
    acc = [0.0, 0.0, 0.0]
    half = win // 2
    for i in range(n):
        lo, hi = max(0, i - half), min(n, i + half + 1)
        for j in range(3):
            acc[j] = sum(track[k][j] for k in range(lo, hi)) / (hi - lo)
        out.append((acc[0], acc[1], acc[2]))
    return out
