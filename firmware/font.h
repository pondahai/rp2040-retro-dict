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
/* 回 1 = 找到並填好 g，0 = 缺字（呼叫端畫空框），負值 = 錯誤。 */
int font_get(font *f, uint32_t cp, font_glyph *g);
/* 只要前進寬度時用這個 —— 斷行會逐字問，不必每次解 256 個像素。
 * 缺字回 cjk_w，與 tools/ui_preview.py 的 wrap() 一致。 */
int font_advance(font *f, uint32_t cp);

/* UTF-8 解碼。回傳吃掉的 byte 數，0 表示到底了；壞的 byte 當成 U+FFFD 吃 1 個。 */
int font_utf8_next(const char *s, uint32_t *cp);

#endif /* FONT_H */
