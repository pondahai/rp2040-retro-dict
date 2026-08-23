#include "ime.h"

#include <string.h>

#include "ime_tables.h"

/* 聲調符號 -> 查詢鍵裡的數字。與上游 formatBopomofoToKey() 一致。 */
static const struct { const char *mark; char digit; } TONES[] = {
    { "\xCB\x8A", '2' },        /* ˊ */
    { "\xCB\x87", '3' },        /* ˇ */
    { "\xCB\x8B", '4' },        /* ˋ */
    { "\xCB\x99", '5' },        /* ˙ */
};

const char *ime_key_bopo(char key)
{
    int i;
    if (key >= 'A' && key <= 'Z')
        key = (char)(key + 32);
    for (i = 0; i < IME_KEYMAP_N; i++)
        if (IME_KEYMAP[i].key == key)
            return IME_KEYMAP[i].bopo;
    return 0;
}

int ime_bopomofo(const char *keys, char *out, int out_size)
{
    int n = 0;
    if (!keys || out_size < 1)
        return 0;
    for (; *keys; keys++) {
        const char *b = ime_key_bopo(*keys);
        int len;
        if (!b)
            continue;
        len = (int)strlen(b);
        if (n + len >= out_size)
            break;
        memcpy(out + n, b, (size_t)len);
        n += len;
    }
    out[n] = 0;
    return n;
}

/* 注音符號串 -> 查詢鍵：聲調符號換成數字，沒打聲調就補 '1'。
 * 這是上游 formatBopomofoToKey() 的規則，碼表就是照這個排序的。 */
static int to_key(const char *bopo, char *key, int key_size)
{
    int n = 0, has_tone = 0;

    while (*bopo && n < key_size - 1) {
        int i, hit = 0;
        for (i = 0; i < 4; i++) {
            if (memcmp(bopo, TONES[i].mark, 2) == 0) {
                key[n++] = TONES[i].digit;
                bopo += 2;
                has_tone = 1;
                hit = 1;
                break;
            }
        }
        if (hit)
            continue;
        /* 注音符號是 3 個 byte 一個（U+3105..U+3129）。 */
        if (n + 3 > key_size - 1)
            break;
        memcpy(key + n, bopo, 3);
        n += 3;
        bopo += 3;
    }
    if (!has_tone && n < key_size - 1)
        key[n++] = '1';
    key[n] = 0;
    return n;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

/* 索引按查詢鍵排序（strcmp 序），所以可以二分搜尋。 */
static const uint8_t *find(const char *key)
{
    int lo = 0, hi = IME_IDX_COUNT - 1;

    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const uint8_t *rec = IME_IDX + (size_t)mid * IME_REC_SIZE;
        uint16_t koff = rd16(rec);
        uint8_t klen = rec[2];
        int cmp = 0;
        int i;

        /* 池子裡的 key 沒有結尾的 0，所以逐 byte 比、比完再比長度。 */
        for (i = 0; i < klen; i++) {
            unsigned char a = (unsigned char)key[i];
            unsigned char b = IME_POOL[koff + i];
            if (a != b) {
                cmp = a < b ? -1 : 1;
                break;
            }
            if (!a)
                break;
        }
        if (!cmp) {
            unsigned char a = (unsigned char)key[klen];
            cmp = a ? 1 : 0;
        }
        if (!cmp)
            return rec;
        if (cmp < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    return 0;
}

int ime_query(const char *keys, char *out, int out_size)
{
    char bopo[IME_MAX_BOPO];
    char key[IME_MAX_BOPO + 2];
    const uint8_t *rec;
    uint16_t off, len;

    if (!keys || !*keys || out_size < 1)
        return 0;
    if (!ime_bopomofo(keys, bopo, sizeof(bopo)))
        return 0;
    to_key(bopo, key, (int)sizeof(key));
    rec = find(key);
    if (!rec)
        return 0;
    off = rd16(rec + 4);
    len = rd16(rec + 6);
    if (len > out_size - 1)
        len = (uint16_t)(out_size - 1);
    memcpy(out, IME_POOL + off, len);
    out[len] = 0;
    return len;
}

int ime_nth(const char *cands, int n, char *out, int out_size)
{
    int i = 0, seen = 0;

    if (!cands || out_size < 1)
        return 0;
    while (cands[i]) {
        unsigned char c = (unsigned char)cands[i];
        int len = (c < 0x80) ? 1 : ((c & 0xE0) == 0xC0) ? 2
                  : ((c & 0xF0) == 0xE0) ? 3 : 4;
        if (seen == n) {
            if (len > out_size - 1)
                return 0;
            memcpy(out, cands + i, (size_t)len);
            out[len] = 0;
            return len;
        }
        i += len;
        seen++;
    }
    return 0;
}
