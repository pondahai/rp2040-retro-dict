/* 共振峰合成器（定點）。對應 tools/synth/voice.py。
 *
 * Python 那份是規格。這份要聽起來一樣 —— firmware/compare_synth.py 會把
 * 兩邊算出的波形逐點比對，不是靠耳朵判斷「差不多」。
 */

#include "synth.h"

#include <math.h>

/* --------------------------------------------------------------------- */
/* 共振器。Klatt 的標準式子，係數 Q12。                                   */
/*                                                                        */
/* 係數含 exp/cos，看起來很貴，但**只在參數變動時算**：共振峰軌跡是平滑的， */
/* 每 32 個取樣點（2ms）更新一次聽不出差別，成本卻降到 1/32。             */

/* 係數精度。**Q12 不夠**：聲門低通的 a 只有 0.000378，Q12 量化後誤差 29%，
 * 整個聲源的頻譜斜率就跑掉了。Q20 把誤差降到 0.01% 以下。
 * 代價是乘法要用 int64 —— 在 195 倍的餘裕下不是問題。 */
#define Q 20
#define QONE (1 << Q)

static void res_coef(syn_res *r, int f, int bw, int sr)
{
    double c = -exp(-2.0 * M_PI * bw / sr);
    double b = 2.0 * exp(-M_PI * bw / sr) * cos(2.0 * M_PI * f / sr);
    double a = 1.0 - b - c;
    r->b = (int32_t)(b * QONE + (b >= 0 ? 0.5 : -0.5));
    r->c = (int32_t)(c * QONE + (c >= 0 ? 0.5 : -0.5));
    r->a = (int32_t)(a * QONE + (a >= 0 ? 0.5 : -0.5));
}

/* 聲門低通用「不做 DC 正規化」的版本：a 直接設 1。
 *
 * 為什麼：那一級的 a 只有 0.000378，Q20 之後脈衝乘上去再右移 20 位只剩
 * 個位數 —— 整個濾波器在最低有效位上運作，量化雜訊蓋過訊號，實測頻譜
 * 質心從 858Hz 掉到 133Hz。不正規化的話 DC 增益是 1/a ≈ 2646，訊號自然
 * 有足夠的位數，而**頻譜形狀完全相同**（只差一個常數倍），振幅反正
 * 後面會正規化掉。 */
static void res_coef_allpole(syn_res *r, int bw, int sr)
{
    double c = -exp(-2.0 * M_PI * bw / sr);
    double b = 2.0 * exp(-M_PI * bw / sr);
    r->b = (int32_t)(b * QONE + 0.5);
    r->c = (int32_t)(c * QONE - 0.5);
    r->a = QONE;
}

static inline int32_t res_run(syn_res *r, int32_t x)
{
    /* int64 累加。共振器在共振點的增益可以到十幾倍，狀態值遠超過輸入，
     * 用 int32 乘會溢位 —— 溢位的症狀是波形突然翻正負，聽起來像爆音。 */
    int64_t acc = (int64_t)r->a * x + (int64_t)r->b * r->y1 +
                  (int64_t)r->c * r->y2;
    int32_t y = (int32_t)(acc >> Q);
    r->y2 = r->y1;
    r->y1 = y;
    return y;
}

/* --------------------------------------------------------------------- */

static inline uint32_t xorshift(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

void syn_init(syn_state *s, uint32_t seed)
{
    syn_res zero = { 0, 0, 0, 0, 0 };
    s->r1 = s->r2 = s->r3 = s->glottal = zero;
    s->rng = seed ? seed : 1u;
    s->phase_q16 = 0;
    s->prev = 0;
}

int syn_ms(int ms)
{
    return (int)((long)SYN_SR * ms / 1000);
}

/* --------------------------------------------------------------------- */

#define SRC_SCALE 3000           /* 聲源振幅。留足夠空間給共振器的增益 */
#define GLOTTAL_GAIN 260

int syn_render(syn_state *s, const syn_frame *fr, int n,
               int32_t *out, int max_out)
{
    int i;
    if (n > max_out)
        n = max_out;

    for (i = 0; i < n; i++) {
        int32_t src, y;

        if ((i % SYN_COEF_INTERVAL) == 0) {
            res_coef(&s->r1, fr->f[0], fr->bw[0], SYN_SR);
            res_coef(&s->r2, fr->f[1], fr->bw[1], SYN_SR);
            res_coef(&s->r3, fr->f[2], fr->bw[2], SYN_SR);
            res_coef_allpole(&s->glottal, SYN_GLOTTAL_BW, SYN_SR);
        }

        if (fr->silent) {
            src = 0;
        } else if (fr->voiced) {
            /* 週期脈衝 + 聲門低通。**低通不是修飾是必要的** ——
             * 裸脈衝串的頻譜一路平到 Nyquist，餵進共振器會把高頻共振峰
             * 全力激發，聽起來就是純粹的嘯音（實測 65% 能量在 3kHz 以上）。 */
            /* 相位用 Q24 不是 Q16。步進的捨入誤差**每個取樣點都往同方向
             * 累積**，Q16 的 0.12% 誤差在一個音節內就足以讓波形整個錯開 ——
             * 症狀是上升調（二聲）的相關係數掉到 0.5，平調卻沒事。 */
            int32_t step = (int32_t)(((int64_t)fr->f0_q8 << 16) / SYN_SR);
            int32_t pulse;
            s->phase_q16 += step;
            if (s->phase_q16 >= (1 << 24)) {
                s->phase_q16 -= (1 << 24);
                pulse = SRC_SCALE;
            } else if (s->phase_q16 < (int32_t)(0.06 * (1 << 24))) {
                pulse = -SRC_SCALE / 4;
            } else {
                pulse = 0;
            }
            src = res_run(&s->glottal, pulse);
        } else {
            src = ((int32_t)(xorshift(&s->rng) & 0xFFFF) - 32768) / 40;
            if (fr->noise_q8 && fr->noise_q8 != 256)
                src = (int32_t)(((int64_t)src * fr->noise_q8) >> 8);
        }

        y = res_run(&s->r1, src);
        y = res_run(&s->r2, y);
        y = res_run(&s->r3, y);

        /* 嘴唇輻射：一階差分（+6dB/oct）。與聲門低通（-12）合起來是 -6，
         * 那才是母音該有的整體斜率。
         *
         * **這裡不夾範圍**。正規化之前就夾到 int16 會把波形削爛 ——
         * 共振器的輸出本來就可能遠大於 int16，響度是最後才決定的。 */
        out[i] = y - s->prev;
        s->prev = y;
    }
    return n;
}

/* --------------------------------------------------------------------- */
/* 響度正規化。三步缺一不可 —— 見 U3-REPORT §4.6。                        */

static double rms_of(const int32_t *buf, int n)
{
    double acc = 0.0;
    int i;
    if (n <= 0)
        return 0.0;
    for (i = 0; i < n; i++)
        acc += (double)buf[i] * buf[i];
    return sqrt(acc / n);
}

static inline int16_t clamp16(double v)
{
    if (v > 32767.0)
        return 32767;
    if (v < -32768.0)
        return -32768;
    return (int16_t)(v < 0 ? v - 0.5 : v + 0.5);
}

void syn_normalize(const int32_t *in, int n, int pre_len,
                   int cons_q8, int quiet_consonant, int16_t *out)
{
    const double target = 32767.0 * SYN_TARGET_RMS_Q15 / 32768.0;
    const double limit = 32767.0 * SYN_SOFT_LIMIT_Q15 / 32768.0;
    const double cons = (double)cons_q8 / 256.0;
    double r, g, gpre = 0.0;
    int i;

    if (n <= 0)
        return;

    /* 1. 母音段拉到目標響度 */
    r = rms_of(in + pre_len, n - pre_len);
    g = r > 0 ? target / r : 0.0;

    /* 2. 聲母段另外算。噪音經過高 Q 共振器後可以比濁音大 1700 倍，
     *    不分開處理的話母音會被整體正規化壓到聽不見 —— 使用者聽判時
     *    的症狀是「只剩啾啾聲」。 */
    if (pre_len > 0) {
        r = rms_of(in, pre_len);
        gpre = r > 0 ? target * cons / r : 0.0;
    }

    for (i = 0; i < n; i++) {
        double v = (double)in[i] * (i < pre_len ? gpre : g);
        /* 3. 軟限幅。RMS 拉平了還不夠 —— 波峰因數因音節而異。
         *    門檻必須是**固定值**；用「相對自己的峰值」跨音節等於沒作用。 */
        v = limit * tanh(v / limit);
        out[i] = clamp16(v);
    }

    /* 4. 限幅會削掉能量，再校一次 */
    {
        double acc = 0.0;
        for (i = 0; i < n; i++)
            acc += (double)out[i] * out[i];
        r = sqrt(acc / n);
        if (r > 0) {
            g = target / r;
            if (g > 1.0)
                g = 1.0;      /* 只縮不放，避免把限幅過的訊號又推爆 */
            for (i = 0; i < n; i++)
                out[i] = clamp16((double)out[i] * g);
        }
    }

    /* 5. 音量包絡。少了它，音節開頭與結尾會有喀噠聲 —— 波形從 0 直接
     *    跳到滿幅。第一版漏掉整段，波形相關係數卡在 0.7。 */
    {
        int atk = syn_ms(12), rel = syn_ms(30);
        for (i = 0; i < n; i++) {
            double gg = 1.0;
            if (i < pre_len && quiet_consonant)
                gg = 0.9;
            if (atk > 0 && i < atk)
                gg *= (double)i / atk;
            if (rel > 0 && i > n - rel)
                gg *= (double)(n - i) / rel;
            if (gg < 0.0)
                gg = 0.0;
            out[i] = clamp16((double)out[i] * gg);
        }
    }
}

/* --------------------------------------------------------------------- */
/* 中文：音節 id -> 波形                                                  */

static void lerp3(uint16_t *dst, const uint16_t *a, const uint16_t *b,
                  int num, int den)
{
    int i;
    for (i = 0; i < 3; i++)
        dst[i] = (uint16_t)(a[i] + (int32_t)(b[i] - a[i]) * num / den);
}

/* 聲調曲線不再直接從 SYN_TONE_CURVE 取 —— 輕聲要用前一個字算出來的
 * 臨時曲線覆蓋（見 syn_syllable_ctx），所以這裡收的是曲線本身。 */
static int tone_f0_q8(const uint16_t (*curve)[2], int npts, int pos_q8)
{
    int i;
    int32_t mult;

    if (npts <= 1)
        return SYN_BASE_F0_Q8;
    if (pos_q8 <= curve[0][0])
        mult = curve[0][1];
    else if (pos_q8 >= curve[npts - 1][0])
        mult = curve[npts - 1][1];
    else {
        mult = curve[npts - 1][1];
        for (i = 0; i < npts - 1; i++) {
            int x0 = curve[i][0], x1 = curve[i + 1][0];
            if (pos_q8 <= x1) {
                int y0 = curve[i][1], y1 = curve[i + 1][1];
                int span = x1 - x0 ? x1 - x0 : 1;
                mult = y0 + (int32_t)(y1 - y0) * (pos_q8 - x0) / span;
                break;
            }
        }
    }
    return (int)(((int64_t)SYN_BASE_F0_Q8 * mult) >> 8);
}

int syn_syllable(syn_state *s, uint16_t syl_id,
                 int32_t *work, int16_t *out, int max_out)
{
    /* 沒有脈絡的單音節：輕聲照原樣唸、不做句末拉長。這是
     * compare_synth.py 逐音節比對走的路徑，行為與移植前完全相同。 */
    return syn_syllable_ctx(s, syl_id, SYN_TONE_RAW, 0,
                            work, out, max_out);
}

int syn_syllable_ctx(syn_state *s, uint16_t syl_id, int prev_tone, int is_final,
                     int32_t *work, int16_t *out, int max_out)
{
    int base = syl_id / 8, tone = syl_id % 8;
    int ini, fin, kind, noise_f, asp;
    const uint8_t *seq;
    int ntargets = 0, i;
    int pre_len = 0, body_n, total, written = 0;
    int dur_ms, npts;
    const uint16_t (*curve)[2];
    uint16_t neutral_curve[SYN_TONE_MAX_PTS][2];
    syn_frame fr;

    if (base >= SYN_ZH_SYLLABLES || tone > 4)
        return -1;

    curve = SYN_TONE_CURVE[tone];
    npts = SYN_TONE_NPTS[tone];
    dur_ms = SYN_TONE_DUR[tone];

    /* --- 輕聲：音高由前一個字決定 --- */
    if (tone == 0 && prev_tone != SYN_TONE_RAW) {
        if (prev_tone < 0 || prev_tone > 4) {
            /* 輕聲不該出現在句首。真的出現就當成半上 —— 與
             * tools/synth/prosody.py 的 plan() 同一條規則。 */
            curve = SYN_TONE_CURVE[3];
            npts = SYN_TONE_NPTS[3];
            dur_ms = SYN_TONE_DUR[3];
        } else {
            /* 兩點的平緩下滑：起點比終點高 0.06 個倍率（Q8 是 15）。 */
            int lvl = SYN_NEUTRAL_AFTER_Q8[prev_tone];
            neutral_curve[0][0] = 0;
            neutral_curve[0][1] = (uint16_t)(lvl + 15);
            neutral_curve[1][0] = 256;
            neutral_curve[1][1] = (uint16_t)lvl;
            curve = (const uint16_t (*)[2])neutral_curve;
            npts = 2;
        }
    }

    /* --- 句末拉長 --- */
    if (is_final)
        dur_ms = dur_ms * SYN_FINAL_LENGTHEN_PCT / 100;
    ini = SYN_ZH_PARTS[base][0];
    fin = SYN_ZH_PARTS[base][1];
    kind = SYN_ZH_INI_KIND[ini];
    noise_f = SYN_ZH_INI_NOISE[ini];
    asp = SYN_ZH_INI_ASP[ini];

    seq = SYN_ZH_FINAL[fin];
    while (ntargets < SYN_ZH_MAX_TARGETS && seq[ntargets] != 0xFF)
        ntargets++;
    if (ntargets == 0)
        return -1;

    /* --- 聲母段 --- */
    {
        int noisy = (kind == SYN_K_STOP || kind == SYN_K_AFFRICATE ||
                     kind == SYN_K_FRICATIVE);
        int closure = 0, burst = 0, aspir = 0;
        if (kind == SYN_K_STOP) {
            closure = syn_ms(35);
            burst = syn_ms(6);
            aspir = asp ? syn_ms(45) : 0;
        } else if (kind == SYN_K_AFFRICATE) {
            closure = syn_ms(25);
            burst = syn_ms(55);
            aspir = asp ? syn_ms(40) : 0;
        } else if (kind == SYN_K_FRICATIVE) {
            burst = syn_ms(90);
        } else if (kind == SYN_K_NASAL) {
            closure = syn_ms(50);
        } else if (kind == SYN_K_LATERAL || kind == SYN_K_APPROX) {
            closure = syn_ms(30);
        }
        pre_len = closure + burst + aspir;
        fr.noise_q8 = 256;              /* 逐格覆蓋，見下面的迴圈 */

        /* 噪音段用寬頻寬。窄共振器打在白噪上會變成有調的鳴響 ——
         * 使用者聽判時形容成「啾啾聲」。 */
        fr.bw[0] = noisy ? SYN_NOISE_BW1 : SYN_VOWEL_BW1;
        fr.bw[1] = noisy ? SYN_NOISE_BW2 : SYN_VOWEL_BW2;
        fr.bw[2] = noisy ? SYN_NOISE_BW3 : SYN_VOWEL_BW3;
        if (noisy) {
            fr.f[0] = (uint16_t)(noise_f ? noise_f : 1000);
            fr.f[1] = (uint16_t)(noise_f > 1200 ? noise_f : 1200);
            fr.f[2] = 2600;
            fr.voiced = 0;
        } else {
            /* 鼻音／邊音／近音是濁的 */
            const uint16_t nas_n[3] = { 250, 1700, 2600 };
            const uint16_t nas_m[3] = { 250, 1100, 2300 };
            const uint16_t lat[3] = { 350, 1100, 2600 };
            const uint16_t apx[3] = { 400, 1300, 1500 };
            const uint16_t *p = (kind == SYN_K_NASAL)
                ? (noise_f == 0 && ini != 0 ? nas_n : nas_m)
                : (kind == SYN_K_LATERAL ? lat : apx);
            for (i = 0; i < 3; i++)
                fr.f[i] = p[i];
            fr.voiced = 1;
        }

        /* 聲調曲線的位置是**相對整個音節**的比例，不是相對聲母段。
         * 第一版拿 i/pre_len 再除 4 去近似，數值剛好接近但邏輯是錯的。 */
        body_n = syn_ms(dur_ms) - pre_len;
        if (body_n < syn_ms(60))
            body_n = syn_ms(60);
        total = pre_len + body_n;

        for (i = 0; i < pre_len && written < max_out; i++) {
            fr.f0_q8 = (uint16_t)tone_f0_q8(
                curve, npts, (int)((int64_t)i * 256 / total));
            /* 爆破段與送氣段的振幅不同 —— 見 syn_frame.noise_q8 */
            if (noisy) {
                int in_burst = (i < closure + burst);
                if (kind == SYN_K_STOP)
                    fr.noise_q8 = in_burst ? SYN_NOISE_STOP_BURST_Q8
                                           : SYN_NOISE_STOP_ASP_Q8;
                else if (kind == SYN_K_AFFRICATE)
                    fr.noise_q8 = in_burst ? SYN_NOISE_AFFR_BURST_Q8
                                           : SYN_NOISE_AFFR_ASP_Q8;
                else
                    fr.noise_q8 = SYN_NOISE_FRICATIVE_Q8;
            }
            fr.silent = (i < closure && kind != SYN_K_NASAL &&
                         kind != SYN_K_LATERAL && kind != SYN_K_APPROX &&
                         kind != SYN_K_NONE) ? 1 : 0;
            written += syn_render(s, &fr, 1, work + written, max_out - written);
        }
    }

    /* --- 母音段 --- */
    fr.bw[0] = SYN_VOWEL_BW1;
    fr.bw[1] = SYN_VOWEL_BW2;
    fr.bw[2] = SYN_VOWEL_BW3;
    fr.voiced = 1;
    fr.silent = 0;

    for (i = 0; i < body_n && written < max_out; i++) {
        /* 共振峰在目標之間滑行 —— 這正是拼接錄音最難處理、而規則合成
         * 免費得到的東西。 */
        int32_t p = (int32_t)i * (ntargets - 1) * 1024 / (body_n > 1 ? body_n - 1 : 1);
        int k = p / 1024;
        int t = p % 1024;
        if (k >= ntargets - 1) {
            k = ntargets - 2 < 0 ? 0 : ntargets - 2;
            t = 1024;
        }
        if (ntargets == 1) {
            fr.f[0] = SYN_ZH_VOWEL[seq[0]][0];
            fr.f[1] = SYN_ZH_VOWEL[seq[0]][1];
            fr.f[2] = SYN_ZH_VOWEL[seq[0]][2];
        } else {
            /* smoothstep，避免轉折點聽起來有稜角 */
            int32_t ts = (int32_t)(((int64_t)t * t * (3 * 1024 - 2 * t)) >> 20);
            lerp3(fr.f, SYN_ZH_VOWEL[seq[k]], SYN_ZH_VOWEL[seq[k + 1]],
                  ts, 1024);
        }
        fr.f0_q8 = (uint16_t)tone_f0_q8(curve, npts,
                                        (int)((int64_t)(pre_len + i) * 256 / total));
        written += syn_render(s, &fr, 1, work + written, max_out - written);
    }

    syn_normalize(work, written, pre_len,
                  SYN_CONSONANT_LEVEL_Q8 * SYN_KIND_LEVEL_Q8[kind] / 256,
                  kind != SYN_K_NASAL && kind != SYN_K_LATERAL &&
                  kind != SYN_K_APPROX && kind != SYN_K_NONE, out);
    return written;
}

/* --------------------------------------------------------------------- */
/* 英文：音素 id -> 波形                                                  */

int syn_en_is_vowel(uint16_t ph_id)
{
    int idx = ph_id / 4;
    if (idx >= SYN_EN_PHONEMES)
        return 0;
    return SYN_EN_IS_VOWEL[idx] || SYN_EN_DIPH[idx][0] != 0xFF;
}

int syn_en_f0_q8(uint16_t ph_id, int decl_q8)
{
    int idx = ph_id / 4, stress = ph_id % 4;
    int32_t f0;
    if (!syn_en_is_vowel(ph_id))
        return 0;
    (void)idx;
    f0 = (int32_t)(((int64_t)SYN_BASE_F0_Q8 *
                    SYN_EN_STRESS_F0_Q8[stress <= 2 ? stress : 0]) >> 8);
    if (decl_q8 > 0 && decl_q8 != 256)
        f0 = (int32_t)(((int64_t)f0 * decl_q8) >> 8);
    return (int)f0;
}

/* 只給測試程式做 A/B 用；韌體從來不改它。 */
static int g_smooth_ms = SYN_EN_SMOOTH_MS;

void syn_en_set_smoothing(int ms)
{
    g_smooth_ms = ms < 0 ? 0 : ms;
}

void syn_en_ctx_init(syn_en_ctx *c, int n_vowels)
{
    c->n_vowels = n_vowels;
    c->seen = 0;
    c->last_f[0] = c->last_f[1] = c->last_f[2] = 0;
    c->has_last = 0;
    c->smooth_ms = g_smooth_ms;
    /* 詞首的子音還沒有母音可以沿用，用基準基頻 —— 對應 Python
     * english.py synth() 裡 last_f0 的初值 BASE_LEVEL。 */
    c->carry_f0_q8 = SYN_BASE_F0_Q8;
}

int syn_en_ctx_f0(syn_en_ctx *c, uint16_t ph_id)
{
    int decl = 256;
    int f0;
    if (c->n_vowels > 1)
        decl = 256 - (int)SYN_EN_DECL_Q8 * c->seen / (c->n_vowels - 1);
    f0 = syn_en_f0_q8(ph_id, decl);
    if (f0) {
        c->carry_f0_q8 = f0;
        c->seen++;
        return f0;
    }
    return c->carry_f0_q8;
}

int syn_en_count_vowels(const uint16_t *ids, int n)
{
    int i, nv = 0;
    for (i = 0; i < n; i++)
        nv += syn_en_is_vowel(ids[i]);
    return nv;
}

int syn_phoneme(syn_state *s, uint16_t ph_id,
                int32_t *work, int16_t *out, int max_out)
{
    /* 沒有脈絡：不降調、不平滑。與移植前完全相同。 */
    return syn_phoneme_ctx(s, ph_id, NULL, work, out, max_out);
}

/* 音素開頭的共振峰平滑。
 *
 * Python 是對整條軌跡做置中的移動平均，前後各一半都會被改到。串流架構
 * 做不到「往回改」—— 前一個音素早就送出去了。這裡改成只在**開頭**
 * smooth_n 個取樣點，從前一個音素的收尾共振峰滑進來，用的是跟母音段
 * 一樣的 smoothstep。消掉邊界跳變的目的相同，數字不會一樣。 */
static void smooth_in(syn_frame *fr, const syn_en_ctx *ctx, int i, int smooth_n)
{
    int32_t t, ts;
    int k;
    if (!ctx || !ctx->has_last || i >= smooth_n || smooth_n <= 0)
        return;
    t = (int32_t)i * 1024 / smooth_n;
    ts = (int32_t)(((int64_t)t * t * (3 * 1024 - 2 * t)) >> 20);
    for (k = 0; k < 3; k++)
        fr->f[k] = (uint16_t)(ctx->last_f[k] +
                              (int32_t)(fr->f[k] - ctx->last_f[k]) * ts / 1024);
}

int syn_phoneme_ctx(syn_state *s, uint16_t ph_id, syn_en_ctx *ctx,
                    int32_t *work, int16_t *out, int max_out)
{
    int idx = ph_id / 4, stress = ph_id % 4;
    int kind, dur, i, written = 0, pre_len = 0;
    int f0_q8 = ctx ? syn_en_ctx_f0(ctx, ph_id) : 0;
    int smooth_n = ctx ? syn_ms(ctx->smooth_ms) : 0;
    syn_frame fr;

    if (idx >= SYN_EN_PHONEMES)
        return -1;
    kind = SYN_EN_KIND[idx];

    fr.bw[0] = SYN_VOWEL_BW1;
    fr.bw[1] = SYN_VOWEL_BW2;
    fr.bw[2] = SYN_VOWEL_BW3;
    fr.silent = 0;
    fr.noise_q8 = 256;

    if (SYN_EN_IS_VOWEL[idx] || SYN_EN_DIPH[idx][0] != 0xFF) {
        int a = idx, b = idx;
        int diph = SYN_EN_DIPH[idx][0] != 0xFF;
        if (diph) {
            a = SYN_EN_DIPH[idx][0];
            b = SYN_EN_DIPH[idx][1];
            dur = SYN_EN_DUR_DIPHTHONG;
        } else {
            dur = SYN_EN_DUR_VOWEL;
            if (SYN_EN_IS_LONG[idx])
                dur = dur * SYN_EN_LONG_FACTOR_Q8 / 256;
        }
        dur = dur * SYN_EN_STRESS_DUR_Q8[stress <= 2 ? stress : 0] / 256;
        dur = syn_ms(dur);
        fr.voiced = 1;
        fr.f0_q8 = (uint16_t)(f0_q8 > 0 ? f0_q8
                   : (int)(((int64_t)SYN_BASE_F0_Q8 *
                            SYN_EN_STRESS_F0_Q8[stress <= 2 ? stress : 0]) >> 8));
        for (i = 0; i < dur && written < max_out; i++) {
            int32_t t = dur > 1 ? (int32_t)i * 1024 / (dur - 1) : 1024;
            int32_t ts = (int32_t)(((int64_t)t * t * (3 * 1024 - 2 * t)) >> 20);
            lerp3(fr.f, SYN_EN_FORMANT[a], SYN_EN_FORMANT[b], ts, 1024);
            smooth_in(&fr, ctx, i, smooth_n);
            written += syn_render(s, &fr, 1, work + written, max_out - written);
        }
    } else {
        int noisy = (kind == SYN_K_STOP || kind == SYN_K_AFFRICATE ||
                     kind == SYN_K_FRICATIVE);
        int closure = 0, burst = 0, aspir = 0;
        int nf = SYN_EN_NOISE[idx];
        if (kind == SYN_K_STOP) {
            closure = syn_ms(SYN_EN_DUR_CLOSURE);
            burst = syn_ms(SYN_EN_DUR_BURST);
            aspir = SYN_EN_ASP[idx] ? syn_ms(SYN_EN_DUR_ASPIRATION) : 0;
        } else if (kind == SYN_K_AFFRICATE) {
            closure = syn_ms(25);
            burst = syn_ms(SYN_EN_DUR_FRICATIVE);
        } else if (kind == SYN_K_FRICATIVE) {
            burst = syn_ms(SYN_EN_DUR_FRICATIVE);
        } else {
            burst = syn_ms(kind == SYN_K_NASAL ? SYN_EN_DUR_NASAL
                                               : SYN_EN_DUR_GLIDE);
        }
        dur = closure + burst + aspir;
        pre_len = dur;

        if (noisy) {
            fr.bw[0] = SYN_NOISE_BW1;
            fr.bw[1] = SYN_NOISE_BW2;
            fr.bw[2] = SYN_NOISE_BW3;
            fr.f[0] = (uint16_t)(nf ? nf : 1000);
            fr.f[1] = (uint16_t)(nf > 1200 ? nf : 1200);
            fr.f[2] = 2600;
            fr.voiced = 0;
        } else {
            for (i = 0; i < 3; i++)
                fr.f[i] = SYN_EN_FORMANT[idx][i];
            fr.voiced = 1;
        }
        /* 子音沿用前一個母音的音高（Python 的 last_f0）。整個詞往下滑的
         * 時候，夾在中間的濁子音跟著滑才不會聽起來一節一節的。 */
        fr.f0_q8 = (uint16_t)(f0_q8 > 0 ? f0_q8 : SYN_BASE_F0_Q8);
        for (i = 0; i < dur && written < max_out; i++) {
            fr.silent = (noisy && i < closure) ? 1 : 0;
            /* 噪音段不滑入：同樣的理由反過來。母音的共振峰滑進 /s/ 的
             * 噪音頻帶，會讓擦音起頭變成低頻的隆隆聲而不是嘶聲 ——
             * 實測 mass 的 /s/ 起頭 1.5kHz 以下能量從 3.2% 變成 63.1%。 */
            if (!noisy)
                smooth_in(&fr, ctx, i, smooth_n);
            written += syn_render(s, &fr, 1, work + written, max_out - written);
        }
    }

    /* 記下收尾的共振峰給下一個音素滑進來。fr.f 在兩條路徑走完之後都
     * 停在這個音素的終點值（母音是雙母音的第二個目標，子音是它自己）。
     *
     * **噪音類的子音兩個方向都不參與平滑**（滑出見這裡，滑入見上面的
     * `if (!noisy)`）。塞音／擦音／塞擦音的 fr.f 不是聲道共振峰，
     * 是噪音頻帶的中心（/s/ 是 5000Hz）—— 那不是「發音器官在哪裡」，沒有
     * 連續性可言。把它當共振峰滑進下一個母音，等於叫一個窄頻寬（母音的
     * bw）、有聲激發的共振器從 5kHz 掃到 700Hz —— 那就是一聲下滑的哨音。
     *
     * 實機症狀：sad 的 æ 起頭 16ms 內 2.5kHz 以上的能量從 0.1% 衝到 75.9%，
     * 而字首的 æ（apple）沒事，因為字首沒有前一個音素可滑。 */
    if (ctx) {
        int noisy_tail = (kind == SYN_K_STOP || kind == SYN_K_AFFRICATE ||
                          kind == SYN_K_FRICATIVE) &&
                         !SYN_EN_IS_VOWEL[idx] && SYN_EN_DIPH[idx][0] == 0xFF;
        if (noisy_tail) {
            ctx->has_last = 0;      /* 下一個音素從自己的共振峰起頭 */
        } else {
            ctx->last_f[0] = fr.f[0];
            ctx->last_f[1] = fr.f[1];
            ctx->last_f[2] = fr.f[2];
            ctx->has_last = 1;
        }
    }

    /* 英文**不套** SYN_KIND_LEVEL_Q8。那張表是從 voice.py 的 KIND_LEVEL 產生
     * 的，而 english.py 的 synth() 是 pre_len=0 呼叫 render()，Python 端的英文
     * 根本不走聲母響度那條路 —— 這裡套下去會製造一個 compare_synth.py 驗不到
     * 的分歧。英文的子音相對響度由 speech.c 的 KIND_GAIN 管。 */
    syn_normalize(work, written, pre_len, SYN_CONSONANT_LEVEL_Q8,
                  kind == SYN_K_STOP || kind == SYN_K_AFFRICATE ||
                  kind == SYN_K_FRICATIVE, out);
    return written;
}
