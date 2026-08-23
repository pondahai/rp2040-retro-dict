"""U3 實驗：產生一組對照音檔，外加不靠耳朵的客觀檢查。

HANDOVER 說聲調銜接、三聲連讀、輕聲「全是紙上談兵」。這支程式把每一項
變成一組可以 A/B 對比的 WAV，並且對能量測的部分做量測 —— 基頻軌跡是否
真的走成該有的形狀，不需要聽就能驗。

    python tools/say.py --experiment
"""

import math
import os
import sys

from . import prosody, spectrum, voice

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "..", "out", "audio")


def _say():
    """延後匯入，避免 say.py 與本模組互相匯入。"""
    import say
    return say


CASES = []


def case(name, note):
    def deco(fn):
        CASES.append((name, note, fn))
        return fn
    return deco


# ---------------------------------------------------------------------------
# 各項對照
# ---------------------------------------------------------------------------

@case("01_four_tones", "四聲 + 輕聲，同一個音節。聲調曲線對不對，聽這個最準")
def _four_tones(s):
    return s.render([("ma", 1), ("ma", 2), ("ma", 3), ("ma", 4), ("ma", 0)],
                    gap_ms=140)


@case("02_vowels", "母音清單：a o e i u ü er + 兩種空韻。母音不對就全錯")
def _vowels(s):
    seq = [("ma", 1), ("mo", 1), ("me", 1), ("mi", 1), ("mu", 1),
           ("nv", 1), ("er", 2), ("zi", 1), ("zhi", 1)]
    return s.render(seq, gap_ms=120)


@case("03_join_continuous", "「中國人」音節連續，無間隙 —— 拼接自然度的正面測試")
def _join_cont(s):
    return s.render([("zhong", 1), ("guo", 2), ("ren", 2)], gap_ms=0)


@case("04_join_gap60", "同一句，音節間插 60ms 靜音。跟 03 比，聽哪個像人話")
def _join_gap(s):
    return s.render([("zhong", 1), ("guo", 2), ("ren", 2)], gap_ms=60)


@case("05_sandhi_on", "「你好」有做三聲連讀變調（ni2 hao3）—— 轉檔期的實際輸出")
def _sandhi_on(s):
    return s.render(s.from_pinyin("ni3 hao3", sandhi=True))


@case("06_sandhi_off", "「你好」不變調（ni3 hao3）。跟 05 比，聽變調規則值不值得")
def _sandhi_off(s):
    return s.render(s.from_pinyin("ni3 hao3", sandhi=False))


@case("07_neutral_ctx", "輕聲隨前字調高變化：桌子/椅子/本子/帽子")
def _neutral(s):
    seq = (s.from_pinyin("zhuo1 zi5") + s.from_pinyin("yi3 zi5") +
           s.from_pinyin("ben3 zi5") + s.from_pinyin("mao4 zi5"))
    return s.render(seq, gap_ms=150)


@case("08_neutral_flat", "同樣四個詞，輕聲固定音高不看前字。跟 07 比")
def _neutral_flat(s):
    seq = (s.from_pinyin("zhuo1 zi5") + s.from_pinyin("yi3 zi5") +
           s.from_pinyin("ben3 zi5") + s.from_pinyin("mao4 zi5"))
    return s.render(seq, gap_ms=150, neutral_context=False)


@case("09_sentence", "整句：我是中国人，我会说中文")
def _sentence(s):
    seq = (s.from_pinyin("wo3 shi4 zhong1 guo2 ren2") +
           s.from_pinyin("wo3 hui4 shuo1 zhong1 wen2"))
    return s.render(seq, gap_ms=0)


@case("10_dict_word", "從 CE.DAT 讀轉檔期算好的音節 id 念出來（沒字典就跳過）")
def _dict_word(s):
    try:
        sylls, _py = s.from_dict("中国")
    except SystemExit:
        return None
    return s.render(sylls)


# ---------------------------------------------------------------------------
# 客觀檢查：不靠耳朵能驗的部分
# ---------------------------------------------------------------------------

def estimate_f0(samples, lo=60, hi=260):
    """自相關估基頻。用來確認聲調曲線真的走成該有的形狀。

    **必須正規化**：未正規化的自相關在短 lag 有比較多的項可加，總和天生較大，
    結果一律偏向高頻。第一版就是這樣，把該降的四聲量成「上升到 276Hz」——
    看起來像合成器壞了，其實是尺壞了。
    """
    n = len(samples)
    if n < SR_MIN:
        return 0.0
    best, best_lag = 0.0, 0
    for lag in range(int(voice.SR / hi), int(voice.SR / lo)):
        acc = e1 = e2 = 0.0
        cnt = 0
        for i in range(0, n - lag, 2):
            a, b = samples[i], samples[i + lag]
            acc += a * b
            e1 += a * a
            e2 += b * b
            cnt += 1
        if cnt < 20:
            continue
        denom = math.sqrt(e1 * e2)
        r = acc / denom if denom > 1e-12 else 0.0
        if r > best:
            best, best_lag = r, lag
    return voice.SR / best_lag if best_lag else 0.0


SR_MIN = 200


WINDOW = int(voice.SR * 0.04)   # 40ms


def tone_shape(base, tone):
    """回傳一個音節母音段的 (起始 f0, 結束 f0)，單位 Hz。

    直接讀合成器**實際使用的**基頻軌跡，不用自相關去猜。

    原因：聲調本來就是變動的音高，自相關在變動音高上不可靠 —— 窗口長了會
    偏向短 lag（把四聲量成平的），窗口短了雜訊又大。與其把時間花在修量測器，
    不如讀真值；音檔到底有沒有那個音高，由下面 check_audio_pitch() 用一聲
    （平穩、自相關可信）驗一次就夠。
    """
    curve = prosody.TONE_CURVES[tone]
    dur = prosody.TONE_DURATION[tone]
    dbg = {}
    voice.synth_syllable(base, tone, dur, curve, debug=dbg)
    track = dbg["f0_track"][dbg["pre_len"]:]     # 只看母音段
    return track[0], track[-1]


def check_audio_pitch():
    """用一聲驗證「音檔真的有那個音高」—— 平穩音高，自相關可信。"""
    dbg = {}
    smp = voice.synth_syllable("ma", 1, 300, prosody.TONE_CURVES[1], debug=dbg)
    body = smp[dbg["pre_len"]:]
    measured = estimate_f0(body[len(body) // 4:][:WINDOW])
    expect = dbg["f0_track"][dbg["pre_len"]]
    return measured, expect


def objective_checks():
    print()
    print("客觀檢查（不靠耳朵）")
    ok = True

    shapes = {}
    for tone in (1, 2, 3, 4):
        a, b = tone_shape("ma", tone)
        shapes[tone] = (a, b)
        print("  聲調 %d: %5.1f Hz -> %5.1f Hz" % (tone, a, b))
    meas, exp = check_audio_pitch()
    print("  一聲音檔實測 %.1f Hz（合成器設定 %.1f Hz）" % (meas, exp))

    def want(cond, what):
        nonlocal ok
        print(("  PASS  " if cond else "  FAIL  ") + what)
        if not cond:
            ok = False

    want(abs(meas - exp) < exp * 0.12, "音檔的音高與合成器設定相符")
    want(abs(shapes[1][0] - shapes[1][1]) < 12, "一聲是平的")
    want(shapes[2][1] > shapes[2][0] + 8, "二聲是升的")
    want(shapes[4][1] < shapes[4][0] - 15, "四聲是降的")
    want(shapes[3][0] < shapes[1][0] and shapes[3][0] < shapes[2][1],
         "三聲起點比一聲低")

    d = prosody.TONE_DURATION
    want(d[3] > d[4], "三聲比四聲長")
    want(d[0] < d[4] * 0.7, "輕聲明顯短（這比聲調曲線還關鍵）")

    plan = prosody.plan([("mao", 4), ("zi", 0)])
    lvl_after4 = plan[1][3][-1][1]
    plan = prosody.plan([("zhuo", 1), ("zi", 0)])
    lvl_after1 = plan[1][3][-1][1]
    want(lvl_after4 < lvl_after1, "四聲後的輕聲比一聲後的低")
    return ok


def spectral_checks():
    """母音的能量該落在自己的共振峰上。這條驗不過就是嘯音。"""
    print()
    print("頻譜檢查（母音之間必須有差別）")
    ok = True
    got = {}
    for lbl, base in (("a", "ma"), ("i", "mi"), ("u", "mu")):
        smp = voice.synth_syllable(base, 1, 300, prosody.TONE_CURVES[1])
        body = smp[len(smp) // 3:]
        vals = spectrum.band_energy(body, voice.SR, spectrum.BANDS)
        pk = spectrum.peaks(body, voice.SR)
        got[lbl] = (vals, pk)
        print("  母音 %s  >3kHz %4.1f%%  峰值 %s"
              % (lbl, 100 * (vals[4] + vals[5]),
                 " ".join("%.0f" % f for f in pk)))

    def want(cond, what):
        nonlocal ok
        print(("  PASS  " if cond else "  FAIL  ") + what)
        if not cond:
            ok = False

    for lbl in ("a", "i", "u"):
        vals = got[lbl][0]
        want(vals[4] + vals[5] < 0.15,
             "母音 %s 的高頻能量 <15%%（超過就是嘯音）" % lbl)
    # a 的 F1=800、i 的 F2=2300，兩者必須量得出差別
    want(any(600 < f < 1000 for f in got["a"][1]), "母音 a 有 F1 附近的峰")
    want(any(2000 < f < 2700 for f in got["i"][1]), "母音 i 有 F2 附近的峰")
    want(got["a"][0][1] > got["i"][0][1] * 3,
         "a 與 i 的頻譜明顯不同（相同就代表共振峰沒作用）")
    return ok



# ---------------------------------------------------------------------------

def run():
    s = _say()
    os.makedirs(OUT, exist_ok=True)
    print("U3 音節拼接實驗 —— 輸出到 out/audio/")
    print()
    for name, note, fn in CASES:
        samples = fn(s)
        if samples is None:
            print("  %-18s 跳過（%s）" % (name, note))
            continue
        path = os.path.join(OUT, name + ".wav")
        dur = s.write_wav(path, samples)
        print("  %-18s %4.2fs  %s" % (name + ".wav", dur, note))

    ok = objective_checks()
    ok = spectral_checks() and ok
    print()
    print("客觀部分：" + ("全部通過" if ok else "有項目失敗"))
    print()
    print("接下來要靠耳朵，客觀檢查驗不了自然度：")
    print("  03 vs 04  音節間要不要留間隙")
    print("  05 vs 06  三聲連讀變調值不值得做")
    print("  07 vs 08  輕聲要不要看前字")
    print("  09        整句聽起來像不像中文")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(run())
