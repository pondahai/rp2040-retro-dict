"""頻譜量測。給 U3 用來判斷合成器輸出對不對，不靠耳朵。

**這個檔案是第二次踩同一個坑之後才生出來的。** 前兩次都是量測工具壞掉、
卻讓人以為合成器壞掉：

  1. 自相關沒正規化 -> 把下降的四聲量成上升（experiment.py 有記）
  2. 粗糙 DFT 每 4 點取樣一次 -> 3kHz 以上全是混疊，量出來 a 和 i 的頻譜
     一模一樣

所以這裡用真正的 radix-2 FFT，不抽樣、不取巧。純標準函式庫。
"""

import cmath
import math


def _next_pow2(n):
    p = 1
    while p < n:
        p *= 2
    return p


def fft(x):
    """迭代式 radix-2 FFT。輸入長度必須是 2 的冪。"""
    n = len(x)
    if n & (n - 1):
        raise ValueError("長度必須是 2 的冪")
    a = [complex(v) for v in x]
    # bit-reversal 重排
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    length = 2
    while length <= n:
        ang = -2 * math.pi / length
        wl = cmath.exp(1j * ang)
        for i in range(0, n, length):
            w = 1 + 0j
            for k in range(i, i + length // 2):
                u = a[k]
                v = a[k + length // 2] * w
                a[k] = u + v
                a[k + length // 2] = u - v
                w *= wl
        length <<= 1
    return a


def _hann(n):
    return [0.5 - 0.5 * math.cos(2 * math.pi * i / (n - 1)) for i in range(n)]


def power_spectrum(samples, sr, n=2048):
    """回傳 [(頻率Hz, 功率), ...]，只到 Nyquist。"""
    n = _next_pow2(min(n, len(samples)))
    seg = list(samples[:n])
    if len(seg) < n:
        seg += [0.0] * (n - len(seg))
    w = _hann(n)
    spec = fft([seg[i] * w[i] for i in range(n)])
    return [(k * sr / n, abs(spec[k]) ** 2) for k in range(n // 2)]


def band_energy(samples, sr, bands, n=2048):
    """各頻帶佔總能量的比例。"""
    ps = power_spectrum(samples, sr, n)
    total = sum(p for _f, p in ps) or 1.0
    out = []
    for lo, hi in bands:
        e = sum(p for f, p in ps if lo <= f < hi)
        out.append(e / total)
    return out


def peaks(samples, sr, count=3, n=2048, min_hz=150):
    """找出前 count 個頻譜峰值 —— 用來驗證共振峰真的在該在的位置。"""
    ps = [(f, p) for f, p in power_spectrum(samples, sr, n) if f >= min_hz]
    found = []
    for i in range(1, len(ps) - 1):
        f, p = ps[i]
        if p > ps[i - 1][1] and p >= ps[i + 1][1]:
            found.append((p, f))
    found.sort(reverse=True)
    # 太靠近的峰只留最強的，避免同一個共振峰被算成好幾個
    out = []
    for p, f in found:
        if all(abs(f - g) > 200 for g in out):
            out.append(f)
        if len(out) == count:
            break
    return sorted(out)


BANDS = [(0, 500), (500, 1000), (1000, 2000), (2000, 3000),
         (3000, 5000), (5000, 8000)]


def report(name, samples, sr):
    vals = band_energy(samples, sr, BANDS)
    print(name)
    for (lo, hi), v in zip(BANDS, vals):
        print("  %5d-%5d Hz  %5.1f%%  %s" % (lo, hi, 100 * v, "#" * int(50 * v)))
    print("  >3kHz 合計 %.1f%%   峰值 %s"
          % (100 * (vals[4] + vals[5]),
             " ".join("%.0f" % f for f in peaks(samples, sr))))
    return vals
