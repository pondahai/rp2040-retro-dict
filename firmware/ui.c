#include "ui.h"

#include <string.h>

const uint32_t UI_BG  = 0x061006;
const uint32_t UI_BAR = 0x122E12;
const uint32_t UI_RAMP[4]     = {0x061006, 0x5F8C5F, 0xA0D2A0, 0xD7FFD7};
const uint32_t UI_DIM_RAMP[4] = {0x061006, 0x375037, 0x5F825F, 0x8CB48C};
const uint32_t UI_BAR_RAMP[4] = {0x122E12, 0x649164, 0xA5D7A5, 0xDCFFDC};

static const uint32_t SEL_BG = 0x102810;   /* 候選清單第一列反白 */

static int utf8_cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }

static void box(const ui_target *t, int x0, int y0, int x1, int y1,
                uint32_t color)
{
    int i;
    for (i = x0; i <= x1; i++) {
        t->pixel(t->ctx, i, y0, color);
        t->pixel(t->ctx, i, y1, color);
    }
    for (i = y0; i <= y1; i++) {
        t->pixel(t->ctx, x0, i, color);
        t->pixel(t->ctx, x1, i, color);
    }
}

int ui_draw_text(const ui_target *t, font *f, int x, int top,
                 const char *utf8, const uint32_t ramp[4])
{
    uint32_t cp;
    int n;

    if (!utf8)
        return x;
    while ((n = font_utf8_next(utf8, &cp)) > 0) {
        font_glyph g;
        utf8 += n;
        if (font_get(f, cp, &g) != 1) {
            /* 缺字畫空框，看得出來是缺字而不是壞掉 */
            box(t, x + 1, top + 2, x + 13, top + 14, ramp[2]);
            x += f->cjk_w;
            continue;
        }
        {
            int y, cx;
            for (y = 0; y < g.cell_h; y++)
                for (cx = 0; cx < g.cell_w; cx++) {
                    uint8_t v = g.rows[y * g.cell_w + cx];
                    if (v)
                        t->pixel(t->ctx, x + cx, top + y, ramp[v]);
                }
        }
        x += g.adv;
    }
    return x;
}

int ui_text_width(font *f, const char *utf8)
{
    int w = 0, n;
    uint32_t cp;
    if (!utf8)
        return 0;
    while ((n = font_utf8_next(utf8, &cp)) > 0) {
        w += font_advance(f, cp);
        utf8 += n;
    }
    return w;
}

void ui_draw_status(const ui_target *t, font *f, const char *status)
{
    int x = UI_W - UI_STATUS_W;

    /* **固定位置**，不是靠右對齊 —— 內容會變長變短（英 / 英大 / 英大 音），
     * 靠右對齊的話整格會左右跳，看起來像畫面在抖。格子固定、內容靠左，
     * 位置就穩定了。
     *
     * 先把整格塗回列底色再畫：字數變少時要蓋掉上一次的殘留。 */
    t->fill(t->ctx, x, UI_H - UI_LINE_H, UI_STATUS_W, UI_LINE_H, UI_BAR);
    if (status)
        ui_draw_text(t, f, x + 3, UI_H - UI_LINE_H + 1, status, UI_BAR_RAMP);
}

int ui_wrap_next(font *f, const char *utf8, int max_w)
{
    int used = 0, w = 0;
    uint32_t cp;
    int n;

    while ((n = font_utf8_next(utf8 + used, &cp)) > 0) {
        int cw = font_advance(f, cp);
        if (w + cw > max_w && used)
            break;
        used += n;
        w += cw;
    }
    return used;
}

/* 內文是一串「行」：中文釋義在前、英文釋義在後，中間空 4 px。
 *
 * 兩段合成同一串而不是各畫各的，是因為捲動要能跨過那個分界 —— 捲到一半
 * 停在中英交界，兩段各自記位置會非常難算。這裡改成掃過去、跳過前 scroll
 * 行、畫到底為止，捲動就只是一個整數。
 *
 * emit 回 0 表示畫不下了，掃描可以提早停。
 */
typedef int (*line_fn)(void *ctx, const char *line, int len,
                       const uint32_t ramp[4], int gap);

static int scan_body(font *f, const ui_entry *e, line_fn emit, void *ctx)
{
    const char *blocks[2];
    const uint32_t *ramps[2];
    int gap_before[2];
    int b, count = 0;

    blocks[0] = e->trans_zh; ramps[0] = UI_RAMP;     gap_before[0] = 0;
    blocks[1] = e->def_en;   ramps[1] = UI_DIM_RAMP; gap_before[1] = 4;

    for (b = 0; b < 2; b++) {
        const char *s = blocks[b];
        int gap = gap_before[b];
        if (!s)
            continue;
        while (*s) {
            const char *nl = strchr(s, '\n');
            const char *end = nl ? nl : s + strlen(s);
            while (s < end) {
                char line[512];
                int avail = (int)(end - s);
                int take = avail < (int)sizeof(line) - 1
                           ? avail : (int)sizeof(line) - 1;
                int n;
                /* 截斷要停在字元邊界上，否則會把一個 UTF-8 序列切一半，
                   斷行寬度就跟著錯（Python 那邊是逐字元處理，沒有這個坑）。 */
                while (take < avail && utf8_cont(s[take]))
                    take--;
                memcpy(line, s, (size_t)take);
                line[take] = 0;
                n = ui_wrap_next(f, line, UI_W - 6);
                if (n <= 0)
                    break;
                count++;
                if (emit && !emit(ctx, s, n, ramps[b], gap))
                    return count;
                gap = 0;
                s += n;
            }
            if (!nl)
                break;
            s = nl + 1;
        }
    }
    return count;
}

int ui_body_lines(font *f, const ui_entry *e)
{
    return scan_body(f, e, NULL, NULL);
}

int ui_body_rows(void)
{
    /* 內文從 LINE_H+3 開始，畫到剩兩列高就停（最下面那條是狀態列）。 */
    return (UI_H - UI_LINE_H * 2 - (UI_LINE_H + 3)) / UI_LINE_H + 1;
}

int ui_cand_rows(void)
{
    return (UI_H - UI_LINE_H * 2 - (UI_LINE_H + 3)) / UI_LINE_H + 1;
}

typedef struct {
    const ui_target *t;
    font *f;
    int y;
    int skip;
} body_ctx;

static int body_emit(void *ctx, const char *line, int len,
                     const uint32_t ramp[4], int gap)
{
    body_ctx *b = (body_ctx *)ctx;
    char buf[512];

    if (b->skip > 0) {
        b->skip--;
        return 1;
    }
    b->y += gap;
    if (b->y > UI_H - UI_LINE_H * 2)
        return 0;
    if (len > (int)sizeof(buf) - 1)
        len = (int)sizeof(buf) - 1;
    memcpy(buf, line, (size_t)len);
    buf[len] = 0;
    ui_draw_text(b->t, b->f, 3, b->y, buf, ramp);
    b->y += UI_LINE_H;
    return 1;
}

void ui_render_result(const ui_target *t, font *f, const ui_entry *e,
                      int scroll, const char *bar, const char *status)
{
    body_ctx b;
    int x;

    t->fill(t->ctx, 0, 0, UI_W, UI_H, UI_BG);
    t->fill(t->ctx, 0, 0, UI_W, UI_LINE_H, UI_BAR);

    x = ui_draw_text(t, f, 3, 1, e->headword, UI_BAR_RAMP);
    if (e->phonetic && e->phonetic[0]) {
        x = ui_draw_text(t, f, x + 10, 1, "[", UI_BAR_RAMP);
        x = ui_draw_text(t, f, x, 1, e->phonetic, UI_BAR_RAMP);
        ui_draw_text(t, f, x, 1, "]", UI_BAR_RAMP);
    }

    b.t = t;
    b.f = f;
    b.y = UI_LINE_H + 3;
    b.skip = scroll;
    scan_body(f, e, body_emit, &b);

    t->fill(t->ctx, 0, UI_H - UI_LINE_H, UI_W, UI_LINE_H, UI_BAR);
    ui_draw_status(t, f, status);
    /* 狀態列的字由呼叫端給 —— 英漢與漢英要顯示不同的東西，而這一層
     * 不知道現在是哪個方向（它連字典都沒有）。
     * 這台機器沒有獨立的 F 鍵，F1~F10 是 Fn + 數字（keys.c 的 fn_translate），
     * 所以呼叫端寫的也該是「Fn+1」而不是「F1」。 */
    ui_draw_text(t, f, 3, UI_H - UI_LINE_H + 1, bar, UI_BAR_RAMP);
}

void ui_render_typing(const ui_target *t, font *f, const char *typed,
                      const ui_cand *rows, int n, int sel, const char *bar,
                      const char *status)
{
    int i, x, y;

    t->fill(t->ctx, 0, 0, UI_W, UI_H, UI_BG);
    t->fill(t->ctx, 0, 0, UI_W, UI_LINE_H, UI_BAR);
    x = ui_draw_text(t, f, 3, 1, "\xe6\x9f\xa5\xe8\xa9\xa2\xef\xbc\x9a ",
                     UI_BAR_RAMP);
    x = ui_draw_text(t, f, x, 1, typed, UI_BAR_RAMP);
    ui_draw_text(t, f, x, 1, "_", UI_BAR_RAMP);

    y = UI_LINE_H + 3;
    for (i = 0; i < n; i++) {
        if (y > UI_H - UI_LINE_H * 2)
            break;
        if (i == sel)
            t->fill(t->ctx, 0, y - 1, UI_W, UI_LINE_H, SEL_BG);
        x = ui_draw_text(t, f, 3, y, rows[i].word, UI_RAMP);
        if (rows[i].trans && rows[i].trans[0]) {
            int avail = UI_W - 8 - x;
            if (avail > 20) {
                char line[512];
                int take = (int)strlen(rows[i].trans);
                int cut;
                if (take > (int)sizeof(line) - 1) {
                    take = (int)sizeof(line) - 1;
                    while (take && utf8_cont(rows[i].trans[take]))
                        take--;
                }
                memcpy(line, rows[i].trans, (size_t)take);
                line[take] = 0;
                cut = ui_wrap_next(f, line, avail);
                line[cut] = 0;
                ui_draw_text(t, f, x + 8, y, line, UI_DIM_RAMP);
            }
        }
        y += UI_LINE_H;
    }

    t->fill(t->ctx, 0, UI_H - UI_LINE_H, UI_W, UI_LINE_H, UI_BAR);
    ui_draw_text(t, f, 3, UI_H - UI_LINE_H + 1, bar, UI_BAR_RAMP);
    ui_draw_status(t, f, status);
}
