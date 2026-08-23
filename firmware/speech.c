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

static int one(speech *sp, uint16_t id, int is_zh)
{
    int n;
    /* 每個音素／音節各自從乾淨的狀態開始 —— 與 Python 參考實作一致
     * （firmware/compare_synth.py 就是這樣比的）。 */
    syn_init(&sp->st, 12345u);
    n = is_zh ? syn_syllable(&sp->st, id, sp->work, sp->seg, sp->max_seg)
              : syn_phoneme(&sp->st, id, sp->work, sp->seg, sp->max_seg);
    if (n < 0)
        return 0;            /* 不認得的 id 就跳過，不要整個詞不出聲 */
    return emit(sp, n);
}

int speech_ids(speech *sp, const uint8_t *ids, int nbytes, int is_zh)
{
    int i;
    sp->samples = 0;
    if (!ids || nbytes < 2)
        return 0;
    for (i = 0; i + 1 < nbytes; i += 2)
        one(sp, (uint16_t)(ids[i] | (ids[i + 1] << 8)), is_zh);
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
            one(sp, SPELL_IDS[row][k], 0);
    }
    return sp->samples;
}
