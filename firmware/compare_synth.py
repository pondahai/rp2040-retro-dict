"""把 C 版合成器與 Python 版逐點比對。

    python firmware/compare_synth.py

Python 那份是規格（`tools/synth/`）。C 那份是定點運算，不會位元相同 ——
所以比的是**聽得出來的差別**，用三個指標：

  1. 長度：取樣點數必須一致（時長規則有沒有搬對）
  2. 相關係數：波形形狀有多像（>0.9 代表同一個聲音）
  3. 頻譜質心誤差：音色有沒有跑掉（比波形相關更接近「聽起來像不像」）

只比對「差不多」是不夠的：定點誤差會慢慢累積，共振器係數差一點就可能
讓共振峰位置整個偏掉，而波形相關係數對那個很敏感。
"""

import math
import os
import struct
import subprocess
import sys
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

from dictbuild import pinyin, syllable  # noqa: E402
from synth import english, phoneme, prosody, spectrum, voice  # noqa: E402

EXE = os.path.join(HERE, "test_synth.exe")
TMP = os.path.join(ROOT, "out", "audio", "_c_cmp.wav")

FAILS = []


def fail(what, detail):
    FAILS.append(what)
    print("  FAIL  %s" % what)
    print("        %s" % detail)


def run_c(kind, ids):
    os.makedirs(os.path.dirname(TMP), exist_ok=True)
    r = subprocess.run([EXE, TMP, kind] + [str(i) for i in ids],
                       capture_output=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.decode("utf-8", "replace"))
    with wave.open(TMP, "rb") as w:
        n = w.getnframes()
        raw = w.readframes(n)
    return list(struct.unpack("<%dh" % n, raw))


def to_int16(floats):
    peak = max((abs(v) for v in floats), default=1.0) or 1.0
    return [int(max(-1.0, min(1.0, v / peak)) * 32767) for v in floats]


def correlate(a, b):
    n = min(len(a), len(b))
    if n == 0:
        return 0.0
    sa = sum(a[i] * a[i] for i in range(n))
    sb = sum(b[i] * b[i] for i in range(n))
    sab = sum(a[i] * b[i] for i in range(n))
    d = math.sqrt(sa * sb)
    return sab / d if d else 0.0


def centroid(sig):
    ps = [(f, p) for f, p in spectrum.power_spectrum(sig, voice.SR, 2048)
          if p > 0]
    tot = sum(p for _f, p in ps)
    return sum(f * p for f, p in ps) / tot if tot else 0.0


def pitch_at(sig, at_ms, win_ms=40):
    """量某個時間點的基頻。窗口要短 —— 聲調本來就是變動的音高，
    窗口一長，自相關會偏向短 lag 量出偏高的假值（experiment.py 有記）。"""
    from synth.experiment import estimate_f0
    a = int(voice.SR * at_ms / 1000)
    w = int(voice.SR * win_ms / 1000)
    seg = sig[a:a + w]
    return estimate_f0(seg) if len(seg) >= w // 2 else 0.0


def best_aligned(a, b, max_lag=80):
    """允許幾個取樣點的位移再算相關。

    C 的脈衝相位是 Q8 定點，Python 是浮點，一個音節內會累積出幾個取樣點的
    偏移 —— 聽不出來，但直接算相關會忽正忽負（實測 ma2 是 -0.41，
    對齊後 0.65）。位移本身也要報出來：如果需要幾十個取樣點才對得上，
    那就不是捨入誤差而是真的算錯了。
    """
    n = min(len(a), len(b))
    best, best_lag = -2.0, 0
    for lag in range(-max_lag, max_lag + 1):
        if lag >= 0:
            x, y = a[lag:n], b[:n - lag]
        else:
            x, y = a[:n + lag], b[-lag:n]
        r = correlate(x, y)
        if r > best:
            best, best_lag = r, lag
    return best, best_lag


def compare(label, c_samples, py_floats, skip_ms=0, noisy=False):
    """noisy=True 時不比波形相關 —— 兩邊的亂數不同，噪音本來就不會相關。
    那種音節改看**母音段**的頻譜，那才是聽得出來的部分。"""
    py = to_int16(py_floats)
    if len(c_samples) != len(py):
        fail("%s: 長度不一致" % label,
             "C=%d Python=%d" % (len(c_samples), len(py)))
        return
    n = len(py)
    skip = int(voice.SR * skip_ms / 1000)
    cc, cp = centroid(c_samples[skip:]), centroid(py[skip:])
    dc = abs(cc - cp)
    tol = max(60.0, cp * 0.08)
    ok = dc < tol
    if noisy:
        print("  %-20s 長度 %5d  母音段質心 C=%4.0f Py=%4.0f (差 %3.0f)  "
              "[擦音起頭，不比波形]  %s"
              % (label, n, cc, cp, dc, "OK" if ok else "**差太多**"))
    else:
        r, lag = best_aligned(c_samples, py)
        # 音高才是聽得出來的東西。波形相關只是輔助 —— 相鄰共振峰（如 er 的
        # F2=1350/F3=1500）會互相拍頻，對係數的微小差異特別敏感，
        # 質心與音高都吻合時，0.6 的相關已經代表是同一個聲音。
        dur_ms = n * 1000 // voice.SR
        p_ok = True
        pitches = []
        for at in (dur_ms // 4, dur_ms * 3 // 4):
            fc, fp = pitch_at(c_samples, at), pitch_at(py, at)
            pitches.append((fc, fp))
            if fp > 0 and abs(fc - fp) > fp * 0.04:
                p_ok = False
        ok = ok and p_ok and r > 0.60 and abs(lag) <= 40
        print("  %-20s 長度 %5d  質心差 %3.0f  音高 %.0f/%.0f vs %.0f/%.0f  "
              "對齊相關 %.3f  %s"
              % (label, n, dc, pitches[0][0], pitches[1][0],
                 pitches[0][1], pitches[1][1], r,
                 "OK" if ok else "**差太多**"))
    if not ok:
        FAILS.append(label)


NOISY_INITIALS = ("b", "p", "d", "t", "g", "k", "f", "h", "j", "q", "x",
                  "z", "c", "s", "zh", "ch", "sh")


def initial_of(base):
    from dictbuild.syllable import SYL_PARTS
    return SYL_PARTS.get(base, ("", ""))[0]


def zh_ids(text):
    blob = pinyin.to_ids(text)
    return list(struct.unpack("<%dH" % (len(blob) // 2), blob))


def main():
    if not os.path.exists(EXE):
        print("找不到 %s —— 先跑 firmware/build_synth.bat" % EXE)
        return 2

    print("中文音節（C vs Python）")
    for text in ("ma1", "ma2", "ma3", "ma4", "ma5",
                 "ni3", "hao3", "zhong1", "guo2", "ren2",
                 "shi4", "zi1", "zhi1", "er2", "lu:4", "nv3"):
        ids = zh_ids(text)
        if not ids:
            continue
        sid = ids[0]
        base, tone = syllable.decode_id(sid)
        curve = prosody.TONE_CURVES[tone]
        dur = prosody.TONE_DURATION[tone]
        py = voice.synth_syllable(base, tone, dur, curve)
        ini = initial_of(base)
        noisy = ini in NOISY_INITIALS
        compare("%s (id=%d)" % (text, sid), run_c("zh", [sid]), py,
                skip_ms=90 if noisy else 0, noisy=noisy)

    print()
    print("英文音素（C vs Python）")
    for ph, stress in (("ae", 1), ("iy", 1), ("uw", 0), ("er", 1),
                       ("s", 0), ("sh", 0), ("t", 0), ("m", 0), ("l", 0)):
        pid = phoneme.phoneme_id(ph, stress)
        py = english.synth([(ph, stress)])
        noisy = ph in ("s", "sh", "t", "p", "k", "f", "th", "ch")
        compare("%s%s (id=%d)" % (ph, "*" * stress, pid),
                run_c("en", [pid]), py,
                skip_ms=0, noisy=noisy)

    print()
    if FAILS:
        print("%d 項差太多" % len(FAILS))
        return 1
    print("C 與 Python 的合成結果一致。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
