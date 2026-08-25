#!/usr/bin/env python3
"""從 Python 的合成器參數產生 C 的表格 header。

    python tools/gen_tables.py

**這些表不該手抄。** 共振峰、音節結構、音素對照加起來好幾百個數字，
手抄到 C 一定會有錯，而且錯了只會表現成「某個字念起來怪怪的」——
沒有人會去逐格核對。

Python 那份是唯一來源；要改參數就改 Python，重跑這支程式。
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from dictbuild import syllable  # noqa: E402
from synth import english, phoneme, prosody, voice  # noqa: E402

OUT = os.path.join(HERE, "..", "firmware", "synth_tables.h")


def q(x, bits=0):
    return int(round(x * (1 << bits)))


def emit(f, line=""):
    f.write(line + "\n")


def formant_rows(d, order):
    return [(name, d[name]) for name in order]


def main():
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        emit(f, "/* 由 tools/gen_tables.py 產生 —— 不要手改。")
        emit(f, " *")
        emit(f, " * 要改參數就改 Python 那邊（tools/synth/），再重跑產生器。")
        emit(f, " * 手抄幾百個數字到 C 一定會錯，而且錯了只會表現成")
        emit(f, " * 「某個字念起來怪怪的」，不會有人去逐格核對。")
        emit(f, " */")
        emit(f, "#ifndef SYNTH_TABLES_H")
        emit(f, "#define SYNTH_TABLES_H")
        emit(f)
        emit(f, "#include <stdint.h>")
        emit(f)
        emit(f, "#define SYN_SR          %d" % voice.SR)
        emit(f, "#define SYN_BASE_F0_Q8  %d   /* %.1f Hz */"
             % (q(voice.BASE_F0, 8), voice.BASE_F0))
        emit(f, "#define SYN_GLOTTAL_BW  %d" % int(voice.GLOTTAL_BW))
        emit(f, "#define SYN_SOFT_LIMIT_Q15 %d" % q(voice.SOFT_LIMIT, 15))
        emit(f, "#define SYN_TARGET_RMS_Q15 %d" % q(voice.TARGET_RMS, 15))
        emit(f, "#define SYN_CONSONANT_LEVEL_Q8 %d" % q(voice.CONSONANT_LEVEL, 8))
        emit(f)
        emit(f, "/* 共振器頻寬。母音窄、噪音寬 —— 窄共振器打在白噪上會變成")
        emit(f, " * 有調的鳴響（實測頻譜平坦度 0.0000，嘶聲應該是 0.2）。 */")
        for i, bw in enumerate(voice.VOWEL_BW):
            emit(f, "#define SYN_VOWEL_BW%d   %d" % (i + 1, int(bw)))
        for i, bw in enumerate(voice.NOISE_BW):
            emit(f, "#define SYN_NOISE_BW%d   %d" % (i + 1, int(bw)))
        emit(f)

        # ---- 中文 ----
        zh_vowels = sorted(voice.VOWELS)
        emit(f, "/* --- 中文 --- */")
        emit(f, "/* 共振峰目標，(F1,F2,F3) Hz */")
        emit(f, "static const uint16_t SYN_ZH_VOWEL[%d][3] = {" % len(zh_vowels))
        for name in zh_vowels:
            F = voice.VOWELS[name]
            emit(f, "    { %4d, %4d, %4d },   /* %s */" % (F[0], F[1], F[2], name))
        emit(f, "};")
        vidx = {n: i for i, n in enumerate(zh_vowels)}
        emit(f)

        # 韻母 -> 目標序列。
        # 空韻（zi/zhi 那個不是真的 i）要在**輸出陣列之前**就併進來，
        # 否則 SYN_ZH_PARTS 會指到陣列外面 —— 第一版就是這樣。
        final_seq = dict(voice.FINALS)
        final_seq["__EMPTY_z"] = "z"
        final_seq["__EMPTY_Z"] = "Z"
        finals = sorted(final_seq)
        maxlen = max(len(final_seq[k]) for k in finals)
        emit(f, "#define SYN_ZH_MAX_TARGETS %d" % maxlen)
        emit(f, "/* 韻母的共振峰目標序列。0xFF 表示序列結束。 */")
        emit(f, "static const uint8_t SYN_ZH_FINAL[%d][%d] = {" % (len(finals), maxlen))
        for k in finals:
            seq = [vidx[c] for c in final_seq[k]]
            seq += [0xFF] * (maxlen - len(seq))
            emit(f, "    { %s },   /* %s */"
                 % (", ".join("0x%02X" % v for v in seq), k))
        emit(f, "};")
        fidx = {n: i for i, n in enumerate(finals)}
        emit(f)

        # 聲母
        kinds = ["none", "stop", "affricate", "fricative", "nasal",
                 "lateral", "approx", "glide"]
        emit(f, "typedef enum {")
        for i, k in enumerate(kinds):
            emit(f, "    SYN_K_%s = %d," % (k.upper(), i))
        emit(f, "} syn_kind;")
        emit(f)
        inits = sorted(voice.INITIALS)
        emit(f, "/* 聲母：(型別, 噪音中心 Hz, 是否送氣) */")
        emit(f, "static const uint8_t  SYN_ZH_INI_KIND[%d] = {" % len(inits))
        emit(f, "    %s" % ", ".join("SYN_K_%s" % voice.INITIALS[k][0].upper()
                                     for k in inits))
        emit(f, "};")
        emit(f, "static const uint16_t SYN_ZH_INI_NOISE[%d] = { %s };"
             % (len(inits), ", ".join(str(voice.INITIALS[k][1]) for k in inits)))
        emit(f, "static const uint8_t  SYN_ZH_INI_ASP[%d] = { %s };"
             % (len(inits), ", ".join("1" if voice.INITIALS[k][2] else "0"
                                      for k in inits)))
        iidx = {n: i for i, n in enumerate(inits)}
        emit(f)

        # 音節表：id -> (聲母 index, 韻母 index)。空韻另外標記。
        emit(f, "/* 音節 id -> (聲母, 韻母)。順序與 dictbuild/syllable.py 的")
        emit(f, " * SYLLABLES 完全一致 —— .DAT 裡存的 id 就是這個順序。 */")
        emit(f, "#define SYN_ZH_SYLLABLES %d" % len(syllable.SYLLABLES))
        emit(f, "static const uint8_t SYN_ZH_PARTS[%d][2] = {"
             % len(syllable.SYLLABLES))
        for s in syllable.SYLLABLES:
            ini, fin = syllable.SYL_PARTS.get(s, ("", "a"))
            if fin == "i" and ini in voice._EMPTY_RIME:
                fin = "__EMPTY_" + voice._EMPTY_RIME[ini]
            assert fin in fidx, "韻母 %r 不在表裡" % fin
            emit(f, "    { %2d, %2d },   /* %s */"
                 % (iidx.get(ini, 0), fidx[fin], s))
        emit(f, "};")
        emit(f)

        emit(f, "/* 空韻（zi/ci/si 與 zhi/chi/shi/ri 的 i 不是真的 i）已經")
        emit(f, " * 併進上面的韻母表，SYN_ZH_PARTS 直接指過去。漏掉這個會讓")
        emit(f, " * 整批極常用音節消失 —— 實測 bu4 shi4 只念得出「不」。 */")
        emit(f)

        # 聲調
        emit(f, "/* 聲調曲線。每個點是 (位置 Q8, 基頻倍率 Q8)。 */")
        tones = [1, 2, 3, 4, 0]
        maxpts = max(len(prosody.TONE_CURVES[t]) for t in tones)
        emit(f, "#define SYN_TONE_MAX_PTS %d" % maxpts)
        emit(f, "static const uint8_t SYN_TONE_NPTS[5] = { %s };"
             % ", ".join(str(len(prosody.TONE_CURVES[t])) for t in (0, 1, 2, 3, 4)))
        emit(f, "static const uint16_t SYN_TONE_CURVE[5][%d][2] = {" % maxpts)
        for t in (0, 1, 2, 3, 4):
            pts = prosody.TONE_CURVES[t]
            cells = ["{ %5d, %5d }" % (q(p, 8), q(v, 8)) for p, v in pts]
            cells += ["{ 0, 0 }"] * (maxpts - len(pts))
            emit(f, "    { %s },   /* 聲調 %d */" % (", ".join(cells), t))
        emit(f, "};")
        emit(f, "static const uint16_t SYN_TONE_DUR[5] = { %s };"
             % ", ".join(str(prosody.TONE_DURATION[t]) for t in (0, 1, 2, 3, 4)))
        emit(f, "static const uint16_t SYN_NEUTRAL_AFTER_Q8[5] = { %s };"
             % ", ".join(str(q(prosody.NEUTRAL_AFTER[t], 8)) for t in (0, 1, 2, 3, 4)))
        # 跨音節的兩條：句末拉長與音節間隙。韌體端在 speech.c/synth.c 用，
        # 百分比而不是 Q8 —— 這樣 C 的整數除法跟 Python 的 int(dur*1.15)
        # 逐格對得上，比對測試才不會差一個取樣點。
        emit(f, "#define SYN_FINAL_LENGTHEN_PCT %d"
             % int(round(prosody.FINAL_LENGTHEN * 100)))
        emit(f, "#define SYN_GAP_MS %d" % prosody.GAP_MS)
        # 各類聲母的響度倍率（Q8），索引就是 SYN_K_*。C 端 syn_normalize()
        # 拿它乘在 SYN_CONSONANT_LEVEL_Q8 上。
        # **順序必須跟 synth_tables.h 的 SYN_K_* 列舉一致**：
        # NONE=0 STOP=1 AFFRICATE=2 FRICATIVE=3 NASAL=4 LATERAL=5 APPROX=6 GLIDE=7
        # 寫反了不會編譯失敗，只會把倍率套到錯的聲母上。
        _kinds = ("none", "stop", "affricate", "fricative", "nasal",
                  "lateral", "approx", "glide")
        emit(f, "static const uint16_t SYN_KIND_LEVEL_Q8[8] = { %s };"
             % ", ".join(str(q(voice.KIND_LEVEL.get(k, 1.0), 8)) for k in _kinds))
        # 各噪音段的振幅（Q8）。以最大的那個（塞音爆破 0.5）當 256，其餘照
        # 比例縮 —— syn_normalize() 會把整個聲母段重新正規化到目標響度，
        # 所以絕對值不重要，重要的是段與段之間的比例。
        _nmax = voice.NOISE_STOP_BURST
        for _name, _v in (("STOP_BURST", voice.NOISE_STOP_BURST),
                          ("STOP_ASP", voice.NOISE_STOP_ASP),
                          ("AFFR_BURST", voice.NOISE_AFFR_BURST),
                          ("AFFR_ASP", voice.NOISE_AFFR_ASP),
                          ("FRICATIVE", voice.NOISE_FRICATIVE)):
            emit(f, "#define SYN_NOISE_%s_Q8 %d" % (_name, q(_v / _nmax, 8)))
        # 單一音節的取樣點上限。**這個一定要由表算出來，不能手寫**：
        # 原本韌體寫死 4000（0.25 秒），是照「音素」的長度抓的，可是中文
        # 三聲就有 300ms，句末拉長後 345ms —— 於是每個三聲都被 syn_syllable()
        # 靜靜截掉一截，沒有任何錯誤訊息。整串比對長度才抓得到。
        longest_ms = int(max(prosody.TONE_DURATION.values())
                         * prosody.FINAL_LENGTHEN)
        emit(f, "#define SYN_MAX_SEG_SAMPLES %d"
             % (-(-longest_ms * voice.SR // 1000) + 64))
        emit(f)

        # ---- 英文 ----
        emit(f, "/* --- 英文 --- */")
        en_names = phoneme.PHONEMES
        emit(f, "#define SYN_EN_PHONEMES %d" % len(en_names))
        emit(f, "/* 音素 id 的順序與 synth/phoneme.py 的 PHONEMES 一致 ——")
        emit(f, " * .DAT 的 SYL_EN 存的就是這個順序的索引。 */")
        emit(f, "static const uint16_t SYN_EN_FORMANT[%d][3] = {" % len(en_names))
        for n in en_names:
            if n in english.VOWELS:
                F = english.VOWELS[n]
            elif n in english._ARTIC:
                F = english._ARTIC[n]
            else:
                F = (0, 0, 0)
            emit(f, "    { %4d, %4d, %4d },   /* %s */" % (F[0], F[1], F[2], n))
        emit(f, "};")
        emit(f)
        emit(f, "static const uint8_t SYN_EN_KIND[%d] = {" % len(en_names))
        rows = []
        for n in en_names:
            if n in english.DIPHTHONGS:
                rows.append("SYN_K_NONE")     # 雙母音另外處理
            elif n in english.VOWELS:
                rows.append("SYN_K_NONE")
            else:
                k = english.CONSONANTS.get(n, ("none", 0, False))[0]
                rows.append("SYN_K_%s" % k.upper())
        emit(f, "    %s" % ", ".join(rows))
        emit(f, "};")
        emit(f, "static const uint16_t SYN_EN_NOISE[%d] = { %s };"
             % (len(en_names),
                ", ".join(str(english.CONSONANTS.get(n, ("", 0, 0))[1])
                          for n in en_names)))
        emit(f, "static const uint8_t SYN_EN_ASP[%d] = { %s };"
             % (len(en_names),
                ", ".join("1" if english.CONSONANTS.get(n, ("", 0, False))[2] else "0"
                          for n in en_names)))
        emit(f)
        emit(f, "/* 是不是母音／雙母音；雙母音的兩個目標 */")
        emit(f, "static const uint8_t SYN_EN_IS_VOWEL[%d] = { %s };"
             % (len(en_names),
                ", ".join("1" if n in english.VOWELS else "0" for n in en_names)))
        pidx = {n: i for i, n in enumerate(en_names)}
        emit(f, "static const uint8_t SYN_EN_DIPH[%d][2] = {" % len(en_names))
        for n in en_names:
            if n in english.DIPHTHONGS:
                a, b = english.DIPHTHONGS[n]
                emit(f, "    { %2d, %2d },   /* %s */" % (pidx[a], pidx[b], n))
            else:
                emit(f, "    { 0xFF, 0xFF },   /* %s */" % n)
        emit(f, "};")
        emit(f, "static const uint8_t SYN_EN_IS_LONG[%d] = { %s };"
             % (len(en_names),
                ", ".join("1" if n in english.LONG_VOWELS else "0"
                          for n in en_names)))
        emit(f)
        emit(f, "#define SYN_EN_DUR_VOWEL      %d" % english.DUR_VOWEL)
        emit(f, "#define SYN_EN_DUR_DIPHTHONG  %d" % english.DUR_DIPHTHONG)
        emit(f, "#define SYN_EN_DUR_CLOSURE    %d" % english.DUR_STOP_CLOSURE)
        emit(f, "#define SYN_EN_DUR_BURST      %d" % english.DUR_BURST)
        emit(f, "#define SYN_EN_DUR_ASPIRATION %d" % english.DUR_ASPIRATION)
        emit(f, "#define SYN_EN_DUR_FRICATIVE  %d" % english.DUR_FRICATIVE)
        emit(f, "#define SYN_EN_DUR_NASAL      %d" % english.DUR_NASAL)
        emit(f, "#define SYN_EN_DUR_GLIDE      %d" % english.DUR_GLIDE)
        emit(f, "#define SYN_EN_LONG_FACTOR_Q8 %d" % q(english.LONG_FACTOR, 8))
        emit(f, "#define SYN_EN_DECL_Q8 %d" % q(english.DECLINATION, 8))
        emit(f, "#define SYN_EN_SMOOTH_MS %d" % english.SMOOTH_MS)
        emit(f, "static const uint16_t SYN_EN_STRESS_DUR_Q8[3] = { %s };"
             % ", ".join(str(q(english.STRESS_DUR[i], 8)) for i in (0, 1, 2)))
        emit(f, "static const uint16_t SYN_EN_STRESS_F0_Q8[3] = { %s };"
             % ", ".join(str(q(english.STRESS_F0[i], 8)) for i in (0, 1, 2)))
        emit(f)
        emit(f, "#endif /* SYNTH_TABLES_H */")

    print("寫出 %s" % os.path.normpath(OUT))
    with open(OUT, encoding="utf-8") as f:
        print("  %d 行" % sum(1 for _ in f))


if __name__ == "__main__":
    main()
