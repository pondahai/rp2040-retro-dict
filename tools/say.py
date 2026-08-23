#!/usr/bin/env python3
"""U3 實驗：把中文念出來，輸出 WAV。

    python tools/say.py 你好                     # 中文，從字典查拼音
    python tools/say.py --pinyin "ni3 hao3"      # 中文，直接給拼音
    python tools/say.py --en hello               # 英文，從字典查音標
    python tools/say.py --ipa "h@lou"            # 英文，直接給音標
    python tools/say.py --experiment             # 產生整組對照 WAV

`你好` 這種查字典的用法會實際走一次 CE.DAT 的 SYL_ZH 欄位 —— 也就是說
它驗證的不只是合成器，還有「轉檔期算好音節 id」那整條路（FORMAT.md §4.2）。
"""

import os
import struct
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dictbuild import container as C, pinyin, syllable  # noqa: E402
from dictbuild.normalize import normalize_ce, normalize_ec  # noqa: E402
from synth import english, phoneme, prosody, voice  # noqa: E402

DICT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "..", "out", "DICT")


def render(sylls, gap_ms=prosody.GAP_MS, **kw):
    """[(音節, 聲調)] -> 取樣點串列。"""
    samples = []
    for base, tone, dur, curve, gap in prosody.plan(sylls, gap_ms=gap_ms, **kw):
        samples += voice.synth_syllable(base, tone, dur, curve)
        if gap:
            samples += [0.0] * int(voice.SR * gap / 1000.0)
    return samples


def write_wav(path, samples):
    peak = max((abs(s) for s in samples), default=1.0) or 1.0
    scale = 0.89 / peak
    frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s * scale)) * 32767))
                      for s in samples)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(voice.SR)
        w.writeframes(frames)
    return len(samples) / voice.SR


def from_pinyin(text, sandhi=True):
    s = pinyin.parse_syllables(text)
    if sandhi:
        s = pinyin.apply_sandhi(s)
    return [(b, t) for b, t in s if b]


def from_dict_en(word):
    """查 EC.DAT 拿轉檔期算好的音素 id。"""
    idx = os.path.join(DICT_DIR, "EC.IDX")
    if not os.path.exists(idx):
        raise SystemExit("找不到 %s —— 先跑 mkdict.py ec" % idx)
    d = C.Dictionary(idx, os.path.join(DICT_DIR, "EC.DAT"))
    hits = d.lookup(normalize_ec(word))
    if not hits:
        d.close()
        raise SystemExit("字典裡沒有「%s」" % word)
    blob = hits[0].fields.get(C.T_SYL_EN, b"")
    ipa = hits[0].fields.get(C.T_PHONETIC, b"").decode("utf-8")
    d.close()
    if not blob:
        raise SystemExit("「%s」在字典裡沒有音標" % word)
    ids = struct.unpack("<%dH" % (len(blob) // 2), blob)
    return [phoneme.decode_id(i) for i in ids], ipa


def from_dict(word):
    """查 CE.DAT 拿轉檔期算好的音節 id。"""
    idx = os.path.join(DICT_DIR, "CE.IDX")
    if not os.path.exists(idx):
        raise SystemExit("找不到 %s —— 先跑 mkdict.py ce" % idx)
    d = C.Dictionary(idx, os.path.join(DICT_DIR, "CE.DAT"))
    hits = d.lookup(normalize_ce(word))
    if not hits:
        d.close()
        raise SystemExit("字典裡沒有「%s」" % word)
    blob = hits[0].fields.get(C.T_SYL_ZH, b"")
    py = hits[0].fields.get(C.T_PINYIN, b"").decode("utf-8")
    d.close()
    ids = struct.unpack("<%dH" % (len(blob) // 2), blob)
    return [syllable.decode_id(i) for i in ids], py


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1

    if argv[1] == "--experiment":
        from synth.experiment import run
        return run()

    outdir = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "..", "out", "audio")
    os.makedirs(outdir, exist_ok=True)

    if argv[1] == "--ipa":
        phones = phoneme.parse(argv[2])
        print("音素：", " ".join(p + "*" * st for p, st in phones))
        path = os.path.join(outdir, "ipa.wav")
        dur = write_wav(path, english.synth(phones))
        print("寫出 %s（%.2f 秒）" % (path, dur))
        return 0

    if argv[1] == "--en":
        phones, ipa = from_dict_en(argv[2])
        print("字典音標：%s" % ipa)
        print("音素：", " ".join(p + "*" * st for p, st in phones))
        path = os.path.join(outdir, argv[2] + "_en.wav")
        dur = write_wav(path, english.synth(phones))
        print("寫出 %s（%.2f 秒）" % (path, dur))
        return 0

    if argv[1] == "--pinyin":
        sylls = from_pinyin(argv[2])
        name = argv[2].replace(" ", "_")
    else:
        sylls, py = from_dict(argv[1])
        print("字典拼音：%s" % py)
        name = "word"

    print("音節：", " ".join("%s%d" % (b, t) for b, t in sylls))
    path = os.path.join(outdir, name + ".wav")
    dur = write_wav(path, render(sylls))
    print("寫出 %s（%.2f 秒）" % (path, dur))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
