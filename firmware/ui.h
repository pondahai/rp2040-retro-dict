/* 字典畫面排版。tools/ui_preview.py 的 C 對照，逐像素相同。
 *
 * 這一層不碰 SD、不碰 ILI9341：字模從 font.h 來，像素往呼叫端給的
 * ui_target 送。所以 PC 上可以畫成 PPM 比對，板子上換成 DMA 送螢幕。
 *
 * 顏色是不透明的 32 bit 值，這一層只是搬運 —— 板子端把 0xRRGGBB 轉成
 * RGB565 再送出去，PC 端直接寫進 PPM。這樣兩邊比對不會被色深截斷弄髒。
 */
#ifndef UI_H_INCLUDED
#define UI_H_INCLUDED

#include <stdint.h>

#include "font.h"

#define UI_W      320
#define UI_H      240
#define UI_LINE_H 19

typedef struct {
    void *ctx;
    void (*fill)(void *ctx, int x, int y, int w, int h, uint32_t color);
    void (*pixel)(void *ctx, int x, int y, uint32_t color);
} ui_target;

/* 四階灰的調色盤。字模的 0..3 直接查表。 */
extern const uint32_t UI_RAMP[4];       /* 內文 */
extern const uint32_t UI_DIM_RAMP[4];   /* 次要內文（英文釋義） */
extern const uint32_t UI_BAR_RAMP[4];   /* 標題／狀態列上的字 */
extern const uint32_t UI_BG;
extern const uint32_t UI_BAR;

/* 畫一行字，不斷行、不裁切（超出畫面的像素會被丟掉）。
 * top 是字格頂端，回傳畫完的 x。 */
int ui_draw_text(const ui_target *t, font *f, int x, int top,
                 const char *utf8, const uint32_t ramp[4]);

/* 斷行：從 utf8 起算，回傳這一行吃掉幾個 byte（永遠 > 0，除非字串空的）。
 * 按實際字寬算，不是按字數 —— 中英混排字寬不同。 */
int ui_wrap_next(font *f, const char *utf8, int max_w);

/* 詞條畫面。欄位是 UTF-8 字串，NULL 表示沒有。 */
typedef struct {
    const char *headword;
    const char *phonetic;
    const char *trans_zh;   /* 段落用 '\n' 分隔 */
    const char *def_en;
} ui_entry;

void ui_render_result(const ui_target *t, font *f, const ui_entry *e);

/* 邊打邊查畫面。第一列反白。 */
typedef struct {
    const char *word;
    const char *trans;      /* 只畫第一行 */
} ui_cand;

void ui_render_typing(const ui_target *t, font *f, const char *typed,
                      const ui_cand *rows, int n);

#endif /* UI_H_INCLUDED */
