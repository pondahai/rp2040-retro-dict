#include "font.h"

#include <string.h>

/* 從 SD 讀是以區塊為單位的，所以所有存取都走這個帶快取的小讀取器。
 * 索引二分搜尋一次只要 2 個 byte，但同一個區塊會被反覆命中。 */
static int fetch(font *f, uint32_t off, uint32_t len, uint8_t *out)
{
    while (len) {
        uint32_t base = off & ~(uint32_t)(FONT_BLOCK - 1);
        uint32_t skip = off - base;
        uint32_t n = FONT_BLOCK - skip;
        if (n > len)
            n = len;
        if (f->cached != base) {
            if (f->read(f->ctx, base, FONT_BLOCK, f->buf) != 0)
                return FONT_E_IO;
            f->cached = base;
            f->reads++;
        }
        memcpy(out, f->buf + skip, n);
        out += n;
        off += n;
        len -= n;
    }
    return FONT_OK;
}

static int read_u16(font *f, uint32_t off, uint16_t *out)
{
    uint8_t b[2];
    int rc = fetch(f, off, 2, b);
    if (rc != FONT_OK)
        return rc;
    *out = (uint16_t)(b[0] | (b[1] << 8));
    return FONT_OK;
}

static uint32_t rd16(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t align4(uint32_t v) { return (v + 3u) & ~3u; }

int font_open(font *f, font_read_fn read, void *ctx)
{
    uint8_t h[64];
    uint32_t per_row;

    memset(f, 0, sizeof(*f));
    f->read = read;
    f->ctx = ctx;
    f->cached = FONT_NO_BLOCK;

    if (fetch(f, 0, sizeof(h), h) != FONT_OK)
        return FONT_E_IO;
    if (memcmp(h, "RDICTFNT", 8) != 0)
        return FONT_E_MAGIC;
    if (rd16(h + 8) != 2)
        return FONT_E_VERSION;

    f->cell_h   = h[10];
    f->cjk_w    = h[11];
    f->narrow_w = h[12];
    f->bits     = h[13];
    f->wide_count   = (uint16_t)rd16(h + 14);
    f->narrow_count = (uint16_t)rd16(h + 16);
    f->wide_stride   = rd32(h + 18);
    f->narrow_stride = rd32(h + 22);

    if (f->bits != 2 || f->cell_h > FONT_MAX_CELL ||
        f->cjk_w > FONT_MAX_CELL || f->narrow_w > FONT_MAX_CELL)
        return FONT_E_GEOM;
    per_row = ((uint32_t)f->cjk_w * f->bits + 7u) / 8u;
    if (f->wide_stride != per_row * f->cell_h)
        return FONT_E_GEOM;

    f->wide_idx   = 64;
    f->narrow_idx = align4(f->wide_idx + (uint32_t)f->wide_count * 2);
    f->narrow_adv = align4(f->narrow_idx + (uint32_t)f->narrow_count * 2);
    f->wide_off   = align4(f->narrow_adv + f->narrow_count);
    f->narrow_off = f->wide_off + (uint32_t)f->wide_count * f->wide_stride;
    return FONT_OK;
}

/* 排序過的碼位表上二分搜尋。找不到回 -1，錯誤回 -2。 */
static int find(font *f, uint32_t base, int count, uint32_t cp)
{
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        uint16_t v;
        if (read_u16(f, base + (uint32_t)mid * 2, &v) != FONT_OK)
            return -2;
        if (v == cp)
            return mid;
        if (v < cp)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

static int unpack(font *f, uint32_t off, int width, font_glyph *g)
{
    uint8_t packed[FONT_MAX_CELL * ((FONT_MAX_CELL * 2 + 7) / 8)];
    int per_row = (width * 2 + 7) / 8;
    int y, x;

    if (fetch(f, off, (uint32_t)per_row * f->cell_h, packed) != FONT_OK)
        return FONT_E_IO;
    for (y = 0; y < f->cell_h; y++) {
        const uint8_t *row = packed + y * per_row;
        for (x = 0; x < width; x++)
            g->rows[y * width + x] = (row[x / 4] >> (6 - (x % 4) * 2)) & 3;
    }
    g->cell_w = (uint8_t)width;
    g->cell_h = f->cell_h;
    return FONT_OK;
}

int font_get(font *f, uint32_t cp, font_glyph *g)
{
    int i;

    /* 窄表先查 —— 與 Python 的 Font.glyph() 同順序。ASCII 收在窄表裡，
     * 反過來查會讓全形／半形的重疊碼位拿到錯的那個。 */
    i = find(f, f->narrow_idx, f->narrow_count, cp);
    if (i == -2)
        return FONT_E_IO;
    if (i >= 0) {
        uint8_t adv;
        if (fetch(f, f->narrow_adv + (uint32_t)i, 1, &adv) != FONT_OK)
            return FONT_E_IO;
        if (unpack(f, f->narrow_off + (uint32_t)i * f->narrow_stride,
                   f->narrow_w, g) != FONT_OK)
            return FONT_E_IO;
        g->adv = adv;
        return 1;
    }

    i = find(f, f->wide_idx, f->wide_count, cp);
    if (i == -2)
        return FONT_E_IO;
    if (i >= 0) {
        if (unpack(f, f->wide_off + (uint32_t)i * f->wide_stride,
                   f->cjk_w, g) != FONT_OK)
            return FONT_E_IO;
        g->adv = f->cjk_w;
        return 1;
    }
    return 0;
}

int font_advance(font *f, uint32_t cp)
{
    int i = find(f, f->narrow_idx, f->narrow_count, cp);
    if (i >= 0) {
        uint8_t adv;
        if (fetch(f, f->narrow_adv + (uint32_t)i, 1, &adv) != FONT_OK)
            return f->cjk_w;
        return adv;
    }
    /* 缺字也回 cjk_w，跟寬字同寬 —— ui_preview.py 的 wrap() 就是這樣算的。 */
    return f->cjk_w;
}

int font_utf8_next(const char *s, uint32_t *cp)
{
    const uint8_t *p = (const uint8_t *)s;
    uint32_t c = p[0];
    int n, i;

    if (!c) {
        *cp = 0;
        return 0;
    }
    if (c < 0x80) { *cp = c; return 1; }
    else if ((c & 0xE0) == 0xC0) { n = 2; c &= 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 3; c &= 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 4; c &= 0x07; }
    else { *cp = 0xFFFD; return 1; }

    for (i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) { *cp = 0xFFFD; return 1; }
        c = (c << 6) | (p[i] & 0x3F);
    }
    *cp = c;
    return n;
}
