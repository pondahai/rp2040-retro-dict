"""U3 實驗：聲調曲線與韻律。

這裡放的就是 HANDOVER 說「全是紙上談兵」的那幾條：聲調銜接、三聲連讀、
輕聲。它們現在變成可以聽的參數，改一個數字就能再聽一次。

**注意**：變調（三聲連讀、不、一）不在這裡 —— 那些在轉檔期就做完了
（FORMAT.md §4.2，`dictbuild/pinyin.py`）。這個模組拿到的 tone 已經是
變調後的結果。分界線很重要：變調是語言規則，屬於資料；聲調曲線是發音特徵，
屬於播放器。
"""

# 五度標調法轉成基頻倍率。1.0 = BASE_F0。
# 55 / 35 / 214 / 51，數字是趙元任的五度值。
_LEVEL = {1: 0.80, 2: 0.92, 3: 1.06, 4: 1.22, 5: 1.40}


def _lv(*steps):
    return [(i / (len(steps) - 1), _LEVEL[s]) for i, s in enumerate(steps)]


TONE_CURVES = {
    1: _lv(5, 5, 5),          # 陰平 55：高平
    2: _lv(3, 4, 5),          # 陽平 35：中升
    3: [(0.0, _LEVEL[2]), (0.35, _LEVEL[1]),
        (0.75, _LEVEL[1]), (1.0, _LEVEL[3])],   # 上聲 214：低降升
    4: _lv(5, 3, 1),          # 去聲 51：全降
    0: _lv(3, 3),             # 輕聲：短、中平，實際高度受前字影響
}

# 音節時長（毫秒）。三聲最長，輕聲最短 —— 這個差距是「聽起來像中文」
# 最關鍵的一項，比聲調曲線本身還重要。
TONE_DURATION = {1: 230, 2: 240, 3: 300, 4: 200, 0: 120}

# 輕聲的實際音高取決於前一個字的調類。這是真的規則，不是隨手設的。
NEUTRAL_AFTER = {1: 0.60, 2: 0.62, 3: 0.75, 4: 0.45, 0: 0.60}

FINAL_LENGTHEN = 1.15   # 句末音節拉長
GAP_MS = 60             # 音節之間的靜音。U3 聽判結論：03（連續）vs 04（60ms）
                        # 聽得出差別，而且有間隙比較像中文，所以 04 成為預設。
                        # experiment.py 仍保留 0 的對照組，要再聽一次就改那裡。


def plan(syllables, gap_ms=GAP_MS, final_lengthen=FINAL_LENGTHEN,
         neutral_context=True):
    """[(音節, 聲調)] -> [(音節, 聲調, 時長ms, f0曲線, 後面接多少靜音ms)]"""
    out = []
    prev_tone = 1
    for i, (base, tone) in enumerate(syllables):
        last = i == len(syllables) - 1
        dur = TONE_DURATION.get(tone, 220)
        curve = TONE_CURVES.get(tone, TONE_CURVES[1])
        if tone == 0:
            if neutral_context:
                lvl = NEUTRAL_AFTER.get(prev_tone, 0.6)
                curve = [(0.0, lvl + 0.06), (1.0, lvl)]
            # 輕聲不該出現在句首，若真的出現就當成半上
            if i == 0:
                curve = TONE_CURVES[3]
                dur = TONE_DURATION[3]
        if last:
            dur = int(dur * final_lengthen)
        out.append((base, tone, dur, curve, 0 if last else gap_ms))
        prev_tone = tone if tone else prev_tone
    return out
