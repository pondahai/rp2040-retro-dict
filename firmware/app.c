#include "app.h"

#include <string.h>

static void copy_field(const dict_record *rec, uint8_t tag, char *buf,
                       int size, const char **out)
{
    uint16_t len;
    const uint8_t *p = dict_field(rec, tag, &len);
    if (!p) {
        buf[0] = 0;
        *out = 0;
        return;
    }
    if (len > size - 1)
        len = (uint16_t)(size - 1);
    memcpy(buf, p, len);
    buf[len] = 0;
    *out = buf;
}

/* 候選清單：只讀索引不讀 .DAT 是不夠的 —— 使用者要看見中文釋義才知道
 * 是不是自己要的那個字，所以每一列還是得抓一次 .DAT。一頁最多 8 列，
 * 每列一次隨機讀，這是「邊打邊查」真正的 SD 成本。 */
static void refresh_cands(app *a)
{
    dict_entry hits[APP_MAX_CANDS];
    uint8_t key[DICT_MAX_KEY];
    uint32_t klen;
    int limit = ui_cand_rows();
    int i, n;

    a->cand_n = 0;
    if (!a->typed_len)
        return;
    if (limit > APP_MAX_CANDS)
        limit = APP_MAX_CANDS;

    klen = dict_normalize_ec(a->typed, key, sizeof(key));
    if (!klen)
        return;
    n = dict_prefix(a->d, key, klen, hits, limit, 128, 1);
    if (n < 0)
        n = 0;

    for (i = 0; i < n; i++) {
        dict_record rec;
        uint32_t kl = 0;
        char *nl;

        while (kl < DICT_KEY24 && hits[i].key24[kl])
            kl++;
        if (kl > APP_MAX_WORD - 1)
            kl = APP_MAX_WORD - 1;
        memcpy(a->cand_word[i], hits[i].key24, kl);
        a->cand_word[i][kl] = 0;
        a->cands[i].word = a->cand_word[i];
        a->cand_off[i] = hits[i].off;
        a->cand_len[i] = hits[i].len;
        a->cand_trans[i][0] = 0;
        a->cands[i].trans = a->cand_trans[i];

        if (hits[i].len <= sizeof(a->blob) &&
            a->d->read_dat(a->d->dat_ctx, hits[i].off, hits[i].len,
                           a->blob) == 0 &&
            dict_record_parse(a->blob, hits[i].len, &rec) == DICT_OK) {
            const char *p;
            copy_field(&rec, DICT_T_TRANS_ZH, a->cand_trans[i],
                       APP_MAX_TRANS, &p);
            /* 候選列只有一行，多段的只取第一段 */
            nl = strchr(a->cand_trans[i], '\n');
            if (nl)
                *nl = 0;
        }
    }
    a->cand_n = n;
    if (a->sel >= n)
        a->sel = n ? n - 1 : 0;
}

static void show_record(app *a, const dict_record *rec_in)
{
    const dict_record *rec = rec_in;

    copy_field(rec, DICT_T_HEADWORD, a->hw, sizeof(a->hw), &a->entry.headword);
    copy_field(rec, DICT_T_PHONETIC, a->ph, sizeof(a->ph), &a->entry.phonetic);
    copy_field(rec, DICT_T_TRANS_ZH, a->zh, sizeof(a->zh), &a->entry.trans_zh);
    copy_field(rec, DICT_T_DEF_EN, a->en, sizeof(a->en), &a->entry.def_en);
    {
        uint16_t n;
        const uint8_t *p = dict_field(rec, DICT_T_SYL_EN, &n);
        if (!p)
            p = dict_field(rec, DICT_T_SYL_ZH, &n);
        a->syl_len = 0;
        if (p) {
            if (n > APP_MAX_SYL)
                n = APP_MAX_SYL & ~1u;          /* 只截在 id 邊界上 */
            memcpy(a->syl, p, n);
            a->syl_len = n;
        }
    }
    a->body_lines = ui_body_lines(a->f, &a->entry);
    a->scroll = 0;
    a->state = APP_RESULT;
}

static int show_word(app *a, const char *word)
{
    uint8_t key[DICT_MAX_KEY];
    uint32_t klen = dict_normalize_ec(word, key, sizeof(key));
    dict_record rec;

    if (!klen)
        return 0;
    if (dict_lookup(a->d, key, klen, a->blob, sizeof(a->blob), &rec) != 1)
        return 0;
    show_record(a, &rec);
    /* 記住這一筆在索引裡的位置，PGUP/PGDN 才知道上一個／下一個是誰。
     * 索引本來就是按鍵排序的，所以「相鄰」就是字母序的前後一個。 */
    a->idx_pos = dict_lower_bound(&a->d->main, key, klen);
    a->has_pos = 1;
    return 1;
}

/* 直接用索引序號取詞 —— 不再查鍵，因為序號本身就是答案。
 * 同一個鍵有多筆（多音字、同形異義詞）時，相鄰序號就是同一個詞的下一個
 * 義項，這正是使用者按 PGDN 會期待的行為。 */
static int show_at(app *a, uint32_t pos)
{
    dict_entry e;
    dict_record rec;

    if (pos >= a->d->main.rec_count)
        return 0;
    if (dict_index_read(&a->d->main, pos, &e) != DICT_OK)
        return 0;
    if (e.len > sizeof(a->blob))
        return 0;
    if (a->d->read_dat(a->d->dat_ctx, e.off, e.len, a->blob) != 0)
        return 0;
    if (dict_record_parse(a->blob, e.len, &rec) != DICT_OK)
        return 0;
    show_record(a, &rec);
    a->idx_pos = pos;
    a->has_pos = 1;
    return 1;
}

void app_init(app *a, dict *d, font *f, const ui_target *t)
{
    memset(a, 0, sizeof(*a));
    a->d = d;
    a->f = f;
    a->t = t;
    a->state = APP_TYPING;
    a->dirty = 1;
}

/* 輸入畫面按 Fn+1 的優先順序：
 *
 *   1. 反白的候選詞有音標（SYL_EN）-> 唸那個，發音正確
 *   2. 沒有 -> 把**使用者打進去的字**交給呼叫端用字母規則推（lts.c）
 *
 * 第 2 條是刻意唸「打進去的字」而不是候選詞：邊打邊唸的時候，使用者想聽的
 * 是自己正在打的東西。ids 為 NULL 就是在說「這個沒有音標，你自己想辦法」。 */
static void speak_typing(app *a)
{
    dict_record rec;
    const uint8_t *syl = 0;
    uint16_t n = 0;

    if (!a->speak)
        return;
    if (a->cand_n && a->sel < a->cand_n &&
        a->cand_len[a->sel] <= sizeof(a->blob) &&
        a->d->read_dat(a->d->dat_ctx, a->cand_off[a->sel],
                       a->cand_len[a->sel], a->blob) == 0 &&
        dict_record_parse(a->blob, a->cand_len[a->sel], &rec) == DICT_OK) {
        syl = dict_field(&rec, DICT_T_SYL_EN, &n);
        if (!syl)
            syl = dict_field(&rec, DICT_T_SYL_ZH, &n);
    }
    if (syl && n >= 2)
        a->speak(a->speak_ctx, syl, n, 0,
                 a->cand_n ? a->cand_word[a->sel] : a->typed);
    else
        a->speak(a->speak_ctx, 0, 0, 0,
                 a->cand_n && a->sel < a->cand_n ? a->cand_word[a->sel]
                                                 : a->typed);
}

static void typing_key(app *a, const key_event *ev)
{
    switch (ev->code) {
    case KEY_BS:
        if (a->typed_len) {
            a->typed[--a->typed_len] = 0;
            a->sel = 0;
            refresh_cands(a);
        }
        break;
    case KEY_ESC:
        a->typed_len = 0;
        a->typed[0] = 0;
        a->sel = 0;
        a->cand_n = 0;
        break;
    case KEY_UP:
        if (a->sel > 0)
            a->sel--;
        break;
    case KEY_DOWN:
        if (a->sel + 1 < a->cand_n)
            a->sel++;
        break;
    case KEY_F1:
        speak_typing(a);
        break;
    case KEY_ENTER:
        /* 有選到候選就查那個，否則查打進去的字 —— 打了半個字直接按
         * ENTER 是常見動作，不該什麼都不發生。 */
        if (a->cand_n && a->sel < a->cand_n)
            show_word(a, a->cand_word[a->sel]);
        else if (a->typed_len)
            show_word(a, a->typed);
        break;
    default:
        if (ev->code >= 0x20 && ev->code < 0x7F &&
            a->typed_len < APP_MAX_TYPED) {
            a->typed[a->typed_len++] = (char)ev->code;
            a->typed[a->typed_len] = 0;
            a->sel = 0;
            refresh_cands(a);
        }
        break;
    }
}

static void result_key(app *a, const key_event *ev)
{
    int max = a->body_lines - ui_body_rows();

    if (max < 0)
        max = 0;

    switch (ev->code) {
    case KEY_UP:
        if (a->scroll > 0)
            a->scroll--;
        break;
    case KEY_DOWN:
        if (a->scroll < max)
            a->scroll++;
        break;
    /* PGUP/PGDN 是**換詞**不是翻頁：紙本字典翻頁就是換到旁邊的詞，而內文
     * 捲動已經有上下鍵了。索引是排序好的，所以序號 ±1 就是字母序的前後一個。 */
    case KEY_PGUP:
        if (a->has_pos && a->idx_pos > 0)
            show_at(a, a->idx_pos - 1);
        break;
    case KEY_PGDN:
        if (a->has_pos)
            show_at(a, a->idx_pos + 1);
        break;
    case KEY_ESC:
    case KEY_BS:
        a->state = APP_TYPING;
        break;
    case KEY_F1:
        if (a->speak)
            a->speak(a->speak_ctx, a->syl_len ? a->syl : 0, a->syl_len, 0,
                     a->entry.headword);
        break;
    default:
        break;
    }
}

void app_key(app *a, const key_event *ev)
{
    if (!ev->code)
        return;
    if (a->state == APP_TYPING)
        typing_key(a, ev);
    else
        result_key(a, ev);
    a->dirty = 1;
}

int app_render(app *a)
{
    if (!a->dirty)
        return 0;
    if (a->state == APP_TYPING)
        ui_render_typing(a->t, a->f, a->typed, a->cands, a->cand_n,
                         a->cand_n ? a->sel : -1);
    else
        ui_render_result(a->t, a->f, &a->entry, a->scroll);
    a->dirty = 0;
    return 1;
}
