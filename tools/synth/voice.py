"""U3 實驗：1980 年代風格的共振峰合成器。

**為什麼不是拼接錄音**

HANDOVER 假設中文發音要錄 1300 個音節再拼接。但當年的電子字典做不到那件事
—— 沒有記憶體放 1300 段錄音。它們用的是共振峰合成：幾個共振器 + 一個嗡嗡作響
的聲源，全部由規則驅動。那個「機器人腔」不是技術限制的副作用，就是這個東西
本身的聲音。

如果它夠好，U1（Ekho 音檔授權不明）整個消失 —— 不需要任何錄音資料，
韌體裡只有一張參數表。

**架構**：source-filter。聲源（週期脈衝 = 濁音／白噪 = 清音）串三個共振器。
這是 Klatt 合成器砍到只剩骨架的版本，純標準函式庫，不用 numpy。
"""

import math
import random

SR = 16000          # 取樣率。當年的裝置就在 8–10kHz，16k 已經是寬裕的
BASE_F0 = 110.0     # 基頻。偏低，聽起來像 80 年代的男聲合成

# (F1, F2, F3) 共振峰目標，單位 Hz。
VOWELS = {
    "a":  (800, 1200, 2800),
    "A":  (750, 1100, 2700),   # ang 裡偏後的 a
    "o":  (500,  850, 2700),
    "e":  (500, 1300, 2500),   # ㄜ
    "E":  (550, 1900, 2600),   # ㄝ，出現在 ie / ue / ian
    "i":  (290, 2300, 3000),
    "u":  (330,  800, 2300),
    "v":  (300, 1900, 2300),   # ü
    "r":  (490, 1350, 1500),   # er，F3 壓低是捲舌的關鍵
    "z":  (350, 1600, 2500),   # 空韻 -i（zi ci si）
    "Z":  (360, 1400, 1900),   # 空韻 -i（zhi chi shi ri），捲舌
    "n":  (250, 1700, 2600),   # -n 韻尾
    "N":  (250,  900, 2300),   # -ng 韻尾
}

# 韻母 -> 共振峰目標序列。書寫形式會騙人，所以用 syllable.SYL_PARTS 給的
# 真正韻母，而不是從拼寫解析。
FINALS = {
    "a": "a", "o": "o", "e": "e", "er": "r",
    "ai": "ai", "ei": "Ei", "ao": "au", "ou": "ou",
    "an": "an", "en": "en", "ang": "AN", "eng": "eN", "ong": "uN",
    "i": "i", "ia": "ia", "ie": "iE", "iao": "iau", "iu": "iou",
    "ian": "iEn", "in": "in", "iang": "iAN", "ing": "iN", "iong": "vuN",
    "io": "io",
    "u": "u", "ua": "ua", "uo": "uo", "uai": "uai", "ui": "uei",
    "uan": "uan", "un": "uen", "uang": "uAN", "ueng": "ueN",
    "v": "v", "ve": "vE", "van": "vEn", "vn": "vn",
}

# 聲母的發音方式。
# 型別：stop 塞音、affricate 塞擦音、fricative 擦音、nasal 鼻音、
#       lateral 邊音、approx 近音、none 零聲母
# 欄位：(型別, 噪音共振峰中心 Hz, 是否送氣)
INITIALS = {
    "":   ("none",      0,    False),
    "b":  ("stop",      800,  False), "p": ("stop", 800, True),
    "d":  ("stop",      1800, False), "t": ("stop", 1800, True),
    "g":  ("stop",      1500, False), "k": ("stop", 1500, True),
    "m":  ("nasal",     0,    False), "n": ("nasal", 0, False),
    "f":  ("fricative", 1200, False),
    "h":  ("fricative", 1000, False),
    "l":  ("lateral",   0,    False),
    "r":  ("approx",    1400, False),
    "j":  ("affricate", 3000, False), "q": ("affricate", 3000, True),
    "x":  ("fricative", 3200, False),
    "z":  ("affricate", 4500, False), "c": ("affricate", 4500, True),
    "s":  ("fricative", 5000, False),
    "zh": ("affricate", 2200, False), "ch": ("affricate", 2200, True),
    "sh": ("fricative", 2400, False),
}

# 空韻要換掉母音：zi/ci/si 與 zhi/chi/shi/ri 的「i」不是真的 i
_EMPTY_RIME = {"z": "z", "c": "z", "s": "z",
               "zh": "Z", "ch": "Z", "sh": "Z", "r": "Z"}


class Resonator:
    """二階共振器。Klatt 的標準式子。"""

    def __init__(self):
        self.y1 = self.y2 = 0.0

    def run(self, x, f, bw):
        c = -math.exp(-2 * math.pi * bw / SR)
        b = 2 * math.exp(-math.pi * bw / SR) * math.cos(2 * math.pi * f / SR)
        a = 1.0 - b - c
        y = a * x + b * self.y1 + c * self.y2
        self.y2 = self.y1
        self.y1 = y
        return y


def _lerp(a, b, t):
    return a + (b - a) * t


def _formant_track(targets, n):
    """把目標序列攤成每個取樣點的 (F1,F2,F3)。

    共振峰是**滑行**過去的，不是跳過去 —— 這正是拼接錄音最難處理、
    而規則合成免費得到的東西。音節內的過渡本來就連續。
    """
    pts = [VOWELS[t] for t in targets]
    if len(pts) == 1:
        return [pts[0]] * n
    out = []
    seg = len(pts) - 1
    for i in range(n):
        p = i / max(1, n - 1) * seg
        k = min(int(p), seg - 1)
        t = p - k
        # 平滑一下，避免轉折點聽起來有稜角
        t = t * t * (3 - 2 * t)
        out.append(tuple(_lerp(pts[k][j], pts[k + 1][j], t) for j in range(3)))
    return out


def _voiced_source(n, f0_track):
    """濁音聲源：週期脈衝。刻意保留豐富諧波，那個嗡嗡聲就是年代感來源。"""
    out = []
    phase = 0.0
    for i in range(n):
        phase += f0_track[i] / SR
        if phase >= 1.0:
            phase -= 1.0
            out.append(1.0)
        else:
            # 脈衝之後補一小段衰減，比純脈衝柔和，不會刺耳
            out.append(-0.25 if phase < 0.06 else 0.0)
    return out


def _noise(n, gain=1.0):
    return [(random.random() * 2 - 1) * gain for _ in range(n)]


def _ms(x):
    return int(SR * x / 1000.0)


def synth_syllable(base, tone, dur_ms, f0_curve, debug=None):
    """合成一個音節。

    f0_curve 是 [(位置0..1, 頻率倍率), ...]，由 prosody 模組依聲調給。
    聲調在這裡只是基頻軌跡 —— 這是規則合成相對拼接錄音的最大優勢：
    **聲調不需要為每個音節各錄五份**。
    """
    from dictbuild.syllable import SYL_PARTS
    ini, fin = SYL_PARTS.get(base, ("", "a"))

    # j/q/x 後面寫成 u 的其實是 ü，生成表已經還原成 v，這裡不必再處理。
    if fin == "i" and ini in _EMPTY_RIME:
        targets = _EMPTY_RIME[ini]
    else:
        targets = FINALS.get(fin, "a")

    kind, noise_f, aspirated = INITIALS.get(ini, ("none", 0, False))

    # --- 聲母段 ---
    pre = []
    if kind == "stop":
        pre += [0.0] * _ms(35)                       # 成阻（靜音）
        pre += _noise(_ms(6), 0.5)                   # 除阻爆破
        if aspirated:
            pre += _noise(_ms(45), 0.28)             # 送氣
    elif kind == "affricate":
        pre += [0.0] * _ms(25)
        pre += _noise(_ms(55), 0.42)
        if aspirated:
            pre += _noise(_ms(40), 0.3)
    elif kind == "fricative":
        pre += _noise(_ms(90), 0.38)
    elif kind == "nasal":
        pre += [0.0] * _ms(50)                       # 鼻音段另外處理
    elif kind in ("lateral", "approx"):
        pre += [0.0] * _ms(30)

    body_n = max(_ms(60), _ms(dur_ms) - len(pre))
    total = len(pre) + body_n

    # --- 基頻軌跡 ---
    f0_track = []
    for i in range(total):
        p = i / max(1, total - 1)
        f0_track.append(BASE_F0 * _interp_curve(f0_curve, p))

    # --- 聲源 ---
    if debug is not None:
        debug["f0_track"] = f0_track      # 給客觀檢查用，見 experiment.py
        debug["pre_len"] = len(pre)
    voiced = _voiced_source(total, f0_track)
    src = []
    for i in range(total):
        if i < len(pre):
            # 聲母段：塞音／擦音用噪音，鼻音與邊音是濁的
            if kind in ("nasal", "lateral", "approx", "none"):
                src.append(voiced[i] * 0.5)
            else:
                src.append(pre[i])
        else:
            src.append(voiced[i])

    # --- 共振峰軌跡 ---
    track = _formant_track(targets, body_n)
    if kind == "nasal":
        # 鼻音段：低鼻腔共振，然後滑進母音
        nas = VOWELS["N"] if ini == "n" else (250, 1100, 2300)
        track = [nas] * len(pre) + track
    elif kind == "lateral":
        track = [(350, 1100, 2600)] * len(pre) + track
    elif kind == "approx":
        track = [(400, 1300, 1500)] * len(pre) + track   # r：F3 低
    else:
        track = [(noise_f or 1000, max(noise_f, 1200), 2600)] * len(pre) + track

    # --- 濾波 ---
    r1, r2, r3 = Resonator(), Resonator(), Resonator()
    out = []
    for i in range(total):
        f1, f2, f3 = track[i]
        y = r1.run(src[i], f1, 70)
        y = r2.run(y, f2, 110)
        y = r3.run(y, f3, 180)
        out.append(y)

    # --- 音量包絡 ---
    atk, rel = _ms(12), _ms(30)
    for i in range(total):
        g = 1.0
        if i < len(pre) and kind not in ("nasal", "lateral", "approx", "none"):
            g = 0.9
        if i < atk:
            g *= i / atk
        if i > total - rel:
            g *= max(0.0, (total - i) / rel)
        out[i] *= g
    return out


def _interp_curve(curve, p):
    if p <= curve[0][0]:
        return curve[0][1]
    for i in range(len(curve) - 1):
        x0, y0 = curve[i]
        x1, y1 = curve[i + 1]
        if p <= x1:
            t = (p - x0) / max(1e-9, x1 - x0)
            return _lerp(y0, y1, t)
    return curve[-1][1]
