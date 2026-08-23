/* 字典查詢後台。對應 tools/dictbuild/container.py 的 reader 部分。
 *
 * Python 那份是規格，這份要與它逐筆一致 —— firmware/test_compare.c 會拿
 * 同一批 .IDX/.DAT 跑幾千次查詢比對兩邊的結果。
 */

#include "dict.h"

/* --------------------------------------------------------------------- */
/* little-endian 讀取。RP2040 本來就是 LE，但寫成函式才不依賴對齊。       */

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int mem_cmp(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

/* --------------------------------------------------------------------- */
/* 扇區來源：單扇區快取。                                                */
/* 這個快取不是可有可無的優化 —— 少了它，二分搜尋收斂後會把同一個扇區    */
/* 再讀 2–3 次，在 SD 上每次都是真的 I/O。見 FORMAT.md §3.2。            */

static const uint8_t *src_sector(dict_source *s, uint32_t n)
{
    if (s->cached == n)
        return s->buf;
    if (s->read(s->ctx, n, s->buf) != 0)
        return 0;
    s->cached = n;
    s->reads++;
    return s->buf;
}

/* --------------------------------------------------------------------- */

static const uint8_t DICT_MAGIC[8] = { 'R','D','I','C','T','I','D','X' };

int dict_index_open(dict_index *ix, dict_read_fn read, void *ctx)
{
    const uint8_t *h;
    uint32_t i;

    ix->src.read = read;
    ix->src.ctx = ctx;
    ix->src.cached = DICT_NO_SECTOR;
    ix->src.reads = 0;

    h = src_sector(&ix->src, 0);
    if (!h)
        return DICT_E_IO;
    for (i = 0; i < 8; i++) {
        if (h[i] != DICT_MAGIC[i])
            return DICT_E_MAGIC;
    }
    if (rd16(h + 8) != 1)
        return DICT_E_VERSION;
    if (rd16(h + 10) != DICT_REC_SIZE)
        return DICT_E_RECSIZE;
    ix->rec_count = rd32(h + 12);
    ix->encoding  = h[16];
    ix->direction = h[17];
    ix->flags     = rd16(h + 18);
    ix->dat_size  = rd32(h + 20);
    return DICT_OK;
}

static void entry_from(const uint8_t *sec, uint32_t slot, dict_entry *out)
{
    const uint8_t *p = sec + slot * DICT_REC_SIZE;
    uint32_t i;
    for (i = 0; i < DICT_KEY24; i++)
        out->key24[i] = p[i];
    out->off  = rd32(p + DICT_KEY24);
    out->len  = rd16(p + DICT_KEY24 + 4);
    out->rank = rd16(p + DICT_KEY24 + 6);
}

int dict_index_read(dict_index *ix, uint32_t i, dict_entry *out)
{
    const uint8_t *sec;
    if (i >= ix->rec_count)
        return DICT_E_NOTFOUND;
    sec = src_sector(&ix->src, 1 + i / DICT_RECS_PER_SEC);
    if (!sec)
        return DICT_E_IO;
    entry_from(sec, i % DICT_RECS_PER_SEC, out);
    return DICT_OK;
}

static void probe_of(const uint8_t *key, uint32_t key_len, uint8_t *probe)
{
    uint32_t i;
    for (i = 0; i < DICT_KEY24; i++)
        probe[i] = (i < key_len) ? key[i] : 0;
}

uint32_t dict_lower_bound(dict_index *ix, const uint8_t *key, uint32_t key_len)
{
    uint8_t probe[DICT_KEY24];
    const uint8_t *sec;
    uint32_t lo, hi, mid, last, i, n;

    if (ix->rec_count == 0)
        return 0;
    probe_of(key, key_len, probe);

    last = (ix->rec_count - 1) / DICT_RECS_PER_SEC;
    lo = 0;
    hi = last;
    /* 找「**最後一個**首筆 <= probe 的扇區」，不是「第一個首筆 >= probe」。
     * 目標通常落在扇區中間，用後者會整個跳過該扇區 —— Python 參考實作
     * 在這裡踩過一次，症狀是「查得到某些字、查不到它旁邊的字」。 */
    while (lo < hi) {
        mid = lo + (hi - lo + 1) / 2;
        sec = src_sector(&ix->src, mid + 1);
        if (!sec)
            return ix->rec_count;
        if (mem_cmp(sec, probe, DICT_KEY24) <= 0)
            lo = mid;
        else
            hi = mid - 1;
    }

    sec = src_sector(&ix->src, lo + 1);
    if (!sec)
        return ix->rec_count;
    n = ix->rec_count - lo * DICT_RECS_PER_SEC;
    if (n > DICT_RECS_PER_SEC)
        n = DICT_RECS_PER_SEC;
    for (i = 0; i < n; i++) {
        if (mem_cmp(sec + i * DICT_REC_SIZE, probe, DICT_KEY24) >= 0)
            return lo * DICT_RECS_PER_SEC + i;
    }
    /* 這個扇區全部小於 probe，答案是下一個扇區的第一筆 */
    {
        uint32_t next = (lo + 1) * DICT_RECS_PER_SEC;
        return next < ix->rec_count ? next : ix->rec_count;
    }
}

/* --------------------------------------------------------------------- */

int dict_record_parse(const uint8_t *blob, uint32_t len, dict_record *rec)
{
    uint16_t total;
    if (len < 3)
        return DICT_E_IO;
    total = rd16(blob);
    if (total > len)
        return DICT_E_IO;
    rec->key_len = blob[2];
    if ((uint32_t)3 + rec->key_len > total)
        return DICT_E_IO;
    rec->key = blob + 3;
    rec->blob = blob;
    rec->blob_len = total;
    rec->rank = 0;
    return DICT_OK;
}

const uint8_t *dict_field(const dict_record *rec, uint8_t tag, uint16_t *len)
{
    uint32_t pos = 3u + rec->key_len;
    while (pos + 3 <= rec->blob_len) {
        uint8_t t = rec->blob[pos];
        uint16_t n = rd16(rec->blob + pos + 1);
        pos += 3;
        if (pos + n > rec->blob_len)
            return 0;
        if (t == tag) {
            if (len)
                *len = n;
            return rec->blob + pos;
        }
        pos += n;          /* 未知 tag 跳過，不報錯（FORMAT.md §4） */
    }
    return 0;
}

/* --------------------------------------------------------------------- */

int dict_lookup_first(dict *d, const uint8_t *key, uint32_t key_len,
                      dict_cursor *cur, uint8_t *buf, uint32_t buf_size,
                      dict_record *rec)
{
    if (key_len == 0)
        return 0;
    probe_of(key, key_len, cur->probe);
    cur->key = key;
    cur->key_len = key_len;
    cur->scanned = 0;
    cur->next = dict_lower_bound(&d->main, key, key_len);
    return dict_lookup_next(d, cur, buf, buf_size, rec);
}

int dict_lookup_next(dict *d, dict_cursor *cur,
                     uint8_t *buf, uint32_t buf_size, dict_record *rec)
{
    dict_entry e;

    while (cur->scanned < DICT_MAX_SCAN) {
        if (dict_index_read(&d->main, cur->next, &e) != DICT_OK)
            return 0;
        if (mem_cmp(e.key24, cur->probe, DICT_KEY24) != 0)
            return 0;
        cur->next++;
        cur->scanned++;
        if (e.len > buf_size)
            return DICT_E_TOOBIG;
        if (d->read_dat(d->dat_ctx, e.off, e.len, buf) != 0)
            return DICT_E_IO;
        if (dict_record_parse(buf, e.len, rec) != DICT_OK)
            return DICT_E_IO;
        /* 截斷鍵的最終比對用完整鍵（FORMAT.md §3.3）。不符的是「剛好前 24
         * bytes 相同」的鄰居，跳過繼續掃，不是結束。 */
        if (rec->key_len == cur->key_len &&
            mem_cmp(rec->key, cur->key, cur->key_len) == 0) {
            rec->rank = e.rank;
            return 1;
        }
    }
    return 0;
}

int dict_lookup(dict *d, const uint8_t *key, uint32_t key_len,
                uint8_t *buf, uint32_t buf_size, dict_record *rec)
{
    dict_cursor cur;
    return dict_lookup_first(d, key, key_len, &cur, buf, buf_size, rec);
}

/* --------------------------------------------------------------------- */

static int has_prefix(const uint8_t *key24, const uint8_t *key, uint32_t n)
{
    uint32_t i;
    if (n > DICT_KEY24)
        n = DICT_KEY24;
    for (i = 0; i < n; i++) {
        if (key24[i] != key[i])
            return 0;
    }
    return 1;
}

static uint32_t key24_len(const uint8_t *k)
{
    uint32_t n = DICT_KEY24;
    while (n > 0 && k[n - 1] == 0)
        n--;
    return n;
}

/* 依 (rank, 長度, 鍵) 排序後取前 limit 筆。插入排序 —— limit 很小，
 * 而且不需要額外記憶體。 */
static int better(const dict_entry *a, const dict_entry *b)
{
    uint32_t la, lb;
    if (a->rank != b->rank)
        return a->rank < b->rank;
    la = key24_len(a->key24);
    lb = key24_len(b->key24);
    if (la != lb)
        return la < lb;
    return mem_cmp(a->key24, b->key24, DICT_KEY24) < 0;
}

static int push_sorted(dict_entry *out, int count, int limit,
                       const dict_entry *e)
{
    int i, j;
    for (i = 0; i < count; i++) {
        if (out[i].off == e->off)
            return count;                    /* 已經有了 */
    }
    if (count == limit && !better(e, &out[count - 1]))
        return count;
    if (count < limit)
        count++;
    for (i = count - 1; i > 0 && better(e, &out[i - 1]); i--)
        out[i] = out[i - 1];
    for (j = count - 1; j > i; j--)
        ;
    out[i] = *e;
    return count;
}

static int scan_into(dict_index *ix, const uint8_t *key, uint32_t key_len,
                     dict_entry *out, int count, int limit, int window)
{
    dict_entry e;
    uint32_t i = dict_lower_bound(ix, key, key_len);
    int scanned = 0;
    while (scanned < window && i < ix->rec_count) {
        if (dict_index_read(ix, i, &e) != DICT_OK)
            break;
        if (!has_prefix(e.key24, key, key_len))
            break;
        count = push_sorted(out, count, limit, &e);
        i++;
        scanned++;
    }
    return count;
}

int dict_prefix(dict *d, const uint8_t *key, uint32_t key_len,
                dict_entry *out, int limit, int window, int common_first)
{
    int count = 0;
    if (key_len == 0 || limit <= 0)
        return 0;
    /* 常用詞索引也要掃窗口再排序，不能只取前 limit 筆 —— 它一樣是按鍵
     * 排序的，只取 limit 筆會漏掉 hello 而拿到 hell/helen（FORMAT.md §8）。 */
    if (common_first && d->has_common)
        count = scan_into(&d->common, key, key_len, out, count, limit, window);
    if (count < limit)
        count = scan_into(&d->main, key, key_len, out, count, limit, window);
    return count;
}

/* --------------------------------------------------------------------- */
/* 正規化。必須與 tools/dictbuild/normalize.py 逐字元一致 —— 不一致會讓
 * 查詢**靜默失敗**（查得到扇區、比不中鍵，看起來就像字典沒收這個字）。 */

static int ec_keep(char c)
{
    if (c >= 'a' && c <= 'z')
        return 1;
    if (c >= '0' && c <= '9')
        return 1;
    return c == ' ' || c == '\'' || c == '.' || c == '-';
}

uint32_t dict_normalize_ec(const char *in, uint8_t *out, uint32_t out_size)
{
    uint32_t n = 0;
    int prev_space = 1;                 /* 開頭視為已有空白 -> 去前導空白 */
    const unsigned char *p = (const unsigned char *)in;

    for (; *p; p++) {
        char c = (char)*p;
        if (*p > 0x7F)
            continue;                   /* 非 ASCII 丟棄，韌體沒有 Unicode 表 */
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        if (c == ' ') {
            if (prev_space)
                continue;
            prev_space = 1;
        } else {
            if (!ec_keep(c))
                continue;
            prev_space = 0;
        }
        if (n + 1 >= out_size)
            break;
        out[n++] = (uint8_t)c;
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    return n;
}
