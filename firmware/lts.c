#include "lts.h"

#include "lts_tables.h"

/* 上下文的符號類別 —— 與 tools/synth/lts.py 的 CLASS 對照。 */
static int cls_match(char sym, char c)
{
    switch (sym) {
    case '#':
        return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
    case '^':
        return c >= 'a' && c <= 'z' &&
               !(c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u');
    case '+':
        return c == 'e' || c == 'i' || c == 'y';
    case '.':
        return c == 'b' || c == 'd' || c == 'v' || c == 'g' || c == 'j' ||
               c == 'l' || c == 'm' || c == 'n' || c == 'r' || c == 'w' ||
               c == 'z';
    default:
        return 0;
    }
}

static int is_class(char sym)
{
    return sym == '#' || sym == '^' || sym == '+' || sym == '.';
}

/* forward = 往右比（右文），否則往左比（左文，樣式要反著讀）。
 * `$` 表示這裡必須就是字首／字尾。 */
static int match_ctx(const char *pat, const char *w, int len, int start,
                     int forward)
{
    int i = start;
    int n = 0;
    int k;

    while (pat[n])
        n++;
    for (k = 0; k < n; k++) {
        char sym = forward ? pat[k] : pat[n - 1 - k];
        if (sym == '$')
            return forward ? (i >= len) : (i < 0);
        if (is_class(sym)) {
            if (forward) {
                if (i >= len || !cls_match(sym, w[i]))
                    return 0;
                i++;
            } else {
                if (i < 0 || !cls_match(sym, w[i]))
                    return 0;
                i--;
            }
        } else {
            if (forward) {
                if (i >= len || w[i] != sym)
                    return 0;
                i++;
            } else {
                if (i < 0 || w[i] != sym)
                    return 0;
                i--;
            }
        }
    }
    return 1;
}

static int starts_with(const char *w, int len, int at, const char *target)
{
    int k = 0;
    while (target[k] && target[k] != ' ') {
        if (at + k >= len || w[at + k] != target[k])
            return 0;
        k++;
    }
    return k;               /* 回傳目標長度 */
}

int lts_word(const char *word, int wlen, uint8_t *out, int max_ph)
{
    char w[LTS_MAX_WORD];
    int len = 0, i, n = 0, k;

    /* 只留字母並轉小寫 —— 數字與標點在這一層沒有意義（呼叫端會逐字母唸）。 */
    for (i = 0; i < wlen && len < LTS_MAX_WORD; i++) {
        char c = word[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c >= 'a' && c <= 'z')
            w[len++] = c;
    }
    if (!len)
        return 0;

    i = 0;
    while (i < len) {
        int hit = 0;
        for (k = 0; k < LTS_RULES; k++) {
            const lts_rule *r = &LTS_RULE[k];
            int tlen = starts_with(w, len, i, r->target);
            int j;
            if (!tlen)
                continue;
            if (r->left[0] && !match_ctx(r->left, w, len, i - 1, 0))
                continue;
            if (r->right[0] && !match_ctx(r->right, w, len, i + tlen, 1))
                continue;
            for (j = 0; j < r->nph && n < max_ph; j++)
                out[n++] = r->ph[j];
            i += tlen;
            hit = 1;
            break;
        }
        if (!hit)
            i++;            /* 沒有規則認得這個字母，跳過 */
    }
    return n;
}

int lts_to_ids(const char *text, uint16_t *out, int max)
{
    uint8_t ph[LTS_MAX_PH_PER_WORD];
    int n = 0;
    int start = 0, i = 0;

    for (;;) {
        char c = text ? text[i] : 0;
        int end = (c == 0 || c == ' ' || c == '-' || c == '/');
        if (end) {
            if (i > start) {
                int m = lts_word(text + start, i - start, ph,
                                 LTS_MAX_PH_PER_WORD);
                int k;
                int stressed = 0;
                for (k = 0; k < m && n < max; k++) {
                    int stress = 0;
                    /* 重音放在**每個單字**的第一個母音上。整串只給一個重音
                     * 會讓後面的字全變成輕聲，片語會唸得像一個超長的詞。 */
                    if (!stressed &&
                        (LTS_VOWEL_MASK >> ph[k]) & 1ULL) {
                        stress = 1;
                        stressed = 1;
                    }
                    out[n++] = (uint16_t)(ph[k] * 4 + stress);
                }
            }
            start = i + 1;
        }
        if (!c)
            break;
        i++;
    }
    return n;
}
