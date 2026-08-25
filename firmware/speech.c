#include <stddef.h>          /* NULL、memcpy —— MSVC 靠別的標頭間接帶進來，
                             * GCC 不會。PC build 過了不代表板子 build 會過。 */
#include <string.h>

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
               syn_en_ctx *en)
{
    int n;
    /* 每個音素／音節都從乾淨的狀態開始。
     *
     * **這一行不能拿掉，理由不只是「跟 Python 參考實作一致」。**
     * 曾經試過整個詞共用一條激發源（Python 的 english.synth() 就是對整個
     * 詞呼叫一次 _voiced_source()），結果爆破音整個不見了 —— apple 的 p、
     * people 的第二個 p 都沒聲音。
     *
     * 原因是 syn_normalize() 一次只看一個音素，而塞音的 pre_len == n，
     * 整段用同一個增益。前一個母音的共振器殘響會被帶進成阻段：實測 apple
     * 的 /p/ 原始 rms 從 47 變成 456（爆破本身沒變，58 -> 55），於是增益
     * 掉了十倍，爆破音被自己的殘響壓死。
     *
     * 要讓連續激發源成立，得整個詞一起正規化 —— 那正是 KIND_GAIN 註解裡
     * 說「做不到」的那件事（int32 工作區放不下整個詞）。所以這兩者是互斥的，
     * 不是還沒做完。共振峰平滑（smooth_in()）不受影響，那個是留著的。 */
    syn_init(&sp->st, 12345u);
    n = is_zh ? syn_syllable_ctx(&sp->st, id, prev_tone, is_final,
                                 sp->work, sp->seg, sp->max_seg)
              : syn_phoneme_ctx(&sp->st, id, en,
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
        one(sp, id, is_zh, prev_tone, is_final, is_zh ? NULL : &en);
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

/* 一個字母／數字的**名字**（a 唸 "ei"、b 唸 "bi:"）。回傳有沒有唸出東西。 */
static int spell_one(speech *sp, char c)
{
    int row, k;
    if (c >= 'A' && c <= 'Z')
        c = (char)(c + 32);
    if (c >= 'a' && c <= 'z')
        row = c - 'a';
    else if (c >= '0' && c <= '9')
        row = 26 + (c - '0');
    else
        return 0;            /* 標點與空白不唸 */
    for (k = 0; k < SPELL_MAX_PH && SPELL_IDS[row][k]; k++)
        /* 逐字母唸不降調：每個字母是各自獨立的一次發音，不是一個詞。 */
        one(sp, SPELL_IDS[row][k], 0, SYN_TONE_NONE, 0, NULL);
    return k > 0;
}

/* 一段（一個「字」）走字母規則。 */
static void letters_one_word(speech *sp, const char *word, int len)
{
    uint16_t ids[LTS_MAX_PH_PER_WORD * 4];
    char buf[LTS_MAX_WORD];
    syn_en_ctx en;
    int n, i;

    if (len <= 0)
        return;
    if (len > (int)sizeof(buf) - 1)
        len = (int)sizeof(buf) - 1;
    memcpy(buf, word, (size_t)len);
    buf[len] = 0;

    n = lts_to_ids(buf, ids, (int)(sizeof(ids) / sizeof(ids[0])));
    syn_en_ctx_init(&en, syn_en_count_vowels(ids, n));
    for (i = 0; i < n; i++)
        one(sp, ids[i], 0, SYN_TONE_NONE, 0, &en);
}

int speech_letters(speech *sp, const char *ascii)
{
    const char *p = ascii;

    sp->samples = 0;
    if (!ascii)
        return 0;

    /* 逐「字」處理，因為**單獨一個字母要唸它的名字，不是字母規則的結果**。
     *
     * lts.c 把 b 推成 /b/ —— 一個 53ms、大半是成阻靜音的爆破音，實機上
     * 幾乎聽不見。但字串裡孤零零的一個字母通常是縮寫（T V、U S A、
     * vitamin C），使用者要聽的是「bi:」這個字母名。
     *
     * 兩個以上字母的才走字母規則：那才是「這個字大概怎麼唸」的問題。 */
    while (*p) {
        const char *start;
        int len;

        while (*p == ' ' || *p == '-')
            p++;
        if (!*p)
            break;
        start = p;
        while (*p && *p != ' ' && *p != '-')
            p++;
        len = (int)(p - start);

        if (len == 1)
            spell_one(sp, start[0]);
        else
            letters_one_word(sp, start, len);
    }
    return sp->samples;
}

int speech_spell(speech *sp, const char *ascii)
{
    sp->samples = 0;
    if (!ascii)
        return 0;
    for (; *ascii; ascii++)
        spell_one(sp, *ascii);
    return sp->samples;
}

