/* FONT.BIN 讀取器（tools/mkfont.py 的 C 對照）。
 *
 * 跟 dict.c 一樣**不知道螢幕存在**：只要一個「從某個 offset 讀 n 個 byte」
 * 的函式，PC 上餵檔案、板子上餵 SD，程式碼相同。
 *
 * 沒有 malloc、沒有浮點數。整個字模檔留在 SD 上，只快取一個 512B 區塊 ——
 * 兩張碼位表都排序過，二分搜尋每次只碰幾個位置。
 */
#ifndef FONT_H
#define FONT_H

#include <stdint.h>

#define FONT_BLOCK    512
#define FONT_MAX_CELL 16      /* 目前格高與最大格寬都是 16 */

typedef enum {
    FONT_OK = 0,
    FONT_E_IO = -1,
    FONT_E_MAGIC = -2,
    FONT_E_VERSION = -3,
    FONT_E_GEOM = -4
} font_err;

/* 讀 len 個 byte。回 0 成功。 */
typedef int (*font_read_fn)(void *ctx, uint32_t off, uint32_t len, uint8_t *out);

typedef struct {
    font_read_fn read;
    void *ctx;
    uint32_t cached;              /* 目前快取的區塊起點，FONT_NO_BLOCK = 無 */
    uint8_t  buf[FONT_BLOCK];
    uint32_t reads;               /* 實際讀取次數，供效能驗證 */

    /* 選用的 RAM 快取（見 font_cache）。沒掛就全部走 SD。 */
    const uint8_t *idx_cache;     /* 兩張碼位表 + 窄字前進寬度，接續擺放 */
    const uint8_t *ascii_cache;   /* ASCII 的字模點陣，可有可無 */
    uint8_t  has_idx, has_ascii;

    uint8_t  cell_h, cjk_w, narrow_w, bits;
    uint16_t wide_count, narrow_count;
    uint32_t wide_stride, narrow_stride;
    uint32_t wide_idx, narrow_idx, narrow_adv, wide_off, narrow_off;
} font;

#define FONT_NO_BLOCK 0xFFFFFFFFu

/* 一個字模。rows 是 0..3 的四階灰，row-major，寬 cell_w、高 cell_h。 */
typedef struct {
    uint8_t adv;                  /* 前進寬度（窄字每個字不同） */
    uint8_t cell_w;
    uint8_t cell_h;
    uint8_t rows[FONT_MAX_CELL * FONT_MAX_CELL];
} font_glyph;

int font_open(font *f, font_read_fn read, void *ctx);

/* 把碼位表搬進 RAM。**這是效能的關鍵，不是可有可無的最佳化。**
 *
 * 沒有它的話，`font_get()` 每查一個字要在 14,516 筆的碼位表上二分搜尋 14 步，
 * 而每一步都落在不同的 512B 區塊上 —— 也就是**每個字十幾次 SD 讀取**。實測
 * 一張「邊打邊查」畫面要讀 2,346 次，在板子上就是每按一個字母卡好幾秒。
 *
 * buf 由呼叫端提供（這一層一樣不 malloc）。要多大用 font_cache_size() 問，
 * 給不夠就只掛得下前面幾張表，給 0 就是全部走 SD。回傳實際用掉幾個 byte。
 *
 * 優先順序：兩張碼位表與窄字前進寬度（查表用，每個字都要）> ASCII 字模
 * （英文畫面的內容幾乎都是它）。漢字字模不快取 —— 14,516 個要 3.5MB。
 */
uint32_t font_cache(font *f, uint8_t *buf, uint32_t size);
/* 全部掛滿要多少 byte。分成兩段：索引段（必要）與 ASCII 段（加分）。 */
uint32_t font_cache_size(const font *f, int with_ascii);
/* 回 1 = 找到並填好 g，0 = 缺字（呼叫端畫空框），負值 = 錯誤。 */
int font_get(font *f, uint32_t cp, font_glyph *g);
/* 只要前進寬度時用這個 —— 斷行會逐字問，不必每次解 256 個像素。
 * 缺字回 cjk_w，與 tools/ui_preview.py 的 wrap() 一致。 */
int font_advance(font *f, uint32_t cp);

/* UTF-8 解碼。回傳吃掉的 byte 數，0 表示到底了；壞的 byte 當成 U+FFFD 吃 1 個。 */
int font_utf8_next(const char *s, uint32_t *cp);

#endif /* FONT_H */
