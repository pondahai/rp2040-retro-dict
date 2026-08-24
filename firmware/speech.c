#include "speech.h"

#include "spell_tables.h"

void speech_init(speech *sp, speech_sink sink, void *ctx,
                 int32_t *work, int16_t *seg, uint8_t *pcm, int max_seg)
{
    sp->sink = sink;
    sp->ctx = ctx;
    sp->work = work;
    sp->seg = seg;
    sp->pcm = pcm;
    sp->max_seg = max_seg;
    sp->samples = 0;
    syn_init(&sp->st, 12345u);
}

/* 一段合成完就立刻送走。整個詞先算完再播的話，"dictionary" 這種長度要
 * 三萬多個取樣點的緩衝區 —— 板子上沒有那麼多 RAM，而且也沒有必要。 */
static int emit(speech *sp, int n)
{
    int i;
    if (n <= 0)
        return 0;
    for (i = 0; i < n; i++) {
        /* int16 -> 8-bit 無號。PWM 的 wrap 是 254，所以 0..254。 */
        int v = (sp->seg[i] >> 8) + 128;
        if (v < 0)
            v = 0;
        else if (v > 254)
            v = 254;
        sp->pcm[i] = (uint8_t)v;
    }
    if (sp->sink)
        sp->sink(sp->ctx, sp->pcm, n);
    sp->samples += n;
    return n;
}

/* 子音相對母音的音量（Q8）。
 *
 * **為什麼需要這張表**：syn_normalize() 是把「一個音素」拉到目標響度，
 * 而我們是一個音素一個音素合成再接起來 —— 於是單獨的 /k/ 爆破音會被放大到
 * 跟母音一樣響，`look` 的字尾聽起來像在敲東西。Python 參考版沒有這個問題，
 * 因為它是**整詞一起**正規化，音素之間的相對響度自然保留。
 *
 * 正確的解法是把整詞規劃移植過來（見 HANDOVER 的待辦），在那之前這張表
 * 用相對音量近似：塞音最小、擦音次之、鼻音與流音接近母音。
 */
#define G(x) ((int)((x) * 256))
static const int16_t KIND_GAIN[] = {
    G(1.00),      /* SYN_K_NONE：母音 */
    G(0.30),      /* SYN_K_STOP：爆破音，就是 look 的 k */
    G(0.45),      /* SYN_K_FRICATIVE */
    G(0.45),      /* SYN_K_AFFRICATE */
    G(0.75),      /* SYN_K_NASAL */
    G(0.80),      /* SYN_K_LATERAL */
    G(0.80),      /* SYN_K_APPROX */
    G(0.85),      /* SYN_K_GLIDE */
};
#undef G

static int gain_of(uint16_t ph_id)
{
    int idx = ph_id / 4;
    int kind;
    if (idx >= (int)(sizeof(SYN_EN_KIND) / sizeof(SYN_EN_KIND[0])))
        return 256;
    kind = SYN_EN_KIND[idx];
    if (kind < 0 || kind >= (int)(sizeof(KIND_GAIN) / sizeof(KIND_GAIN[0])))
        return 256;
    return KIND_GAIN[kind];
}

/* 音節之間的靜音。U3 聽判判定 04（有間隙）比 03（連續）像中文，
 * 所以這是預設行為而不是選項。長度由 tools/synth/prosody.py 的 GAP_MS
 * 產生（synth_tables.h），兩邊不會各說各話。 */
static void emit_gap(speech *sp, int ms)
{
    int n = syn_ms(ms);
    while (n > 0) {
        int k = n > sp->max_seg ? sp->max_seg : n;
        int i;
        for (i = 0; i < k; i++)
            sp->pcm[i] = 128;        /* 8-bit 無號的零位 */
        if (sp->sink)
            sp->sink(sp->ctx, sp->pcm, k);
        sp->samples += k;
        n -= k;
    }
}

static int one(speech *sp, uint16_t id, int is_zh, int prev_tone, int is_final,
               int f0_q8)
{
    int n;
    /* 每個音素／音節各自從乾淨的狀態開始 —— 與 Python 參考實作一致
     * （firmware/compare_synth.py 就是這樣比的）。 */
    syn_init(&sp->st, 12345u);
    n = is_zh ? syn_syllable_ctx(&sp->st, id, prev_tone, is_final,
                                 sp->work, sp->seg, sp->max_seg)
              : syn_phoneme_ctx(&sp->st, id, f0_q8,
                                sp->work, sp->seg, sp->max_seg);
    if (n < 0)
        return 0;            /* 不認得的 id 就跳過，不要整個詞不出聲 */
    if (!is_zh) {
        int g = gain_of(id);
        if (g != 256) {
            int i;
            for (i = 0; i < n; i++)
                sp->seg[i] = (int16_t)((sp->seg[i] * g) >> 8);
        }
    }
    return emit(sp, n);
}

int speech_ids(speech *sp, const uint8_t *ids, int nbytes, int is_zh)
{
    int i, last;
    int prev_tone = SYN_TONE_NONE;
    syn_en_ctx en;

    sp->samples = 0;
    if (!ids || nbytes < 2)
        return 0;

    /* 前掃數母音 —— 降調要知道總數才算得出每個母音降多少。 */
    syn_en_ctx_init(&en, 0);
    if (!is_zh) {
        int nv = 0;
        for (i = 0; i + 1 < nbytes; i += 2)
            nv += syn_en_is_vowel((uint16_t)(ids[i] | (ids[i + 1] << 8)));
        syn_en_ctx_init(&en, nv);
    }

    last = ((nbytes / 2) - 1) * 2;
    for (i = 0; i + 1 < nbytes; i += 2) {
        uint16_t id = (uint16_t)(ids[i] | (ids[i + 1] << 8));
        int is_final = (i == last);
        one(sp, id, is_zh, prev_tone, is_final, is_zh ? 0 : syn_en_ctx_f0(&en, id));
        if (is_zh) {
            int tone = id % 8;
            /* 輕聲不改變脈絡：「本子」的子看的是「本」。與 prosody.py
             * 的 plan() 同一條（prev_tone = tone if tone else prev_tone）。 */
            if (tone != 0)
                prev_tone = tone;
            if (!is_final && SYN_GAP_MS > 0)
                emit_gap(sp, SYN_GAP_MS);
        }
    }
    return sp->samples;
}

int speech_letters(speech *sp, const char *ascii)
{
    uint16_t ids[LTS_MAX_PH_PER_WORD * 4];
    int n, i;

    sp->samples = 0;
    if (!ascii)
        return 0;
    n = lts_to_ids(ascii, ids, (int)(sizeof(ids) / sizeof(ids[0])));
    {
        syn_en_ctx en;
        int nv = 0;
        for (i = 0; i < n; i++)
            nv += syn_en_is_vowel(ids[i]);
        syn_en_ctx_init(&en, nv);
        for (i = 0; i < n; i++)
            one(sp, ids[i], 0, SYN_TONE_NONE, 0, syn_en_ctx_f0(&en, ids[i]));
    }
    return sp->samples;
}

int speech_spell(speech *sp, const char *ascii)
{
    sp->samples = 0;
    if (!ascii)
        return 0;
    for (; *ascii; ascii++) {
        char c = *ascii;
        int row;
        int k;
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c >= 'a' && c <= 'z')
            row = c - 'a';
        else if (c >= '0' && c <= '9')
            row = 26 + (c - '0');
        else
            continue;        /* 標點與空白不唸 */
        for (k = 0; k < SPELL_MAX_PH && SPELL_IDS[row][k]; k++)
            /* 逐字母唸不降調：每個字母是各自獨立的一次發音，
             * 不是一個詞。 */
            one(sp, SPELL_IDS[row][k], 0, SYN_TONE_NONE, 0, 0);
    }
    return sp->samples;
}
