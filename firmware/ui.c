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

/* 把一段文字（可能含 '\n'）畫成多行。y 用傳參更新。
 * limit_y >= 0 時，畫到超過就停 —— 英文釋義那一區會用到。 */
static void draw_para(const ui_target *t, font *f, const char *s, int *y,
                      const uint32_t ramp[4], int limit_y)
{
    char line[512];

    if (!s)
        return;
    while (*s) {
        const char *nl = strchr(s, '\n');
        const char *end = nl ? nl : s + strlen(s);
        while (s < end) {
            int n;
            char save;
            /* 每段獨立斷行：先把段落尾端當成字串結尾 */
            int avail = (int)(end - s);
            int take = avail < (int)sizeof(line) - 1 ? avail : (int)sizeof(line) - 1;
            /* 截斷要停在字元邊界上，否則會把一個 UTF-8 序列切一半，
               斷行寬度就跟著錯（Python 那邊是逐字元處理，沒有這個坑）。 */
            while (take < avail && (utf8_cont(s[take])))
                take--;
            memcpy(line, s, (size_t)take);
            line[take] = 0;
            n = ui_wrap_next(f, line, UI_W - 6);
            if (n <= 0)
                break;
            save = line[n];
            line[n] = 0;
            if (limit_y >= 0 && *y > limit_y)
                return;
            ui_draw_text(t, f, 3, *y, line, ramp);
            line[n] = save;
            *y += UI_LINE_H;
            s += n;
        }
        if (!nl)
            break;
        s = nl + 1;
    }
}

void ui_render_result(const ui_target *t, font *f, const ui_entry *e)
{
    int x, y;

    t->fill(t->ctx, 0, 0, UI_W, UI_H, UI_BG);
    t->fill(t->ctx, 0, 0, UI_W, UI_LINE_H, UI_BAR);

    x = ui_draw_text(t, f, 3, 1, e->headword, UI_BAR_RAMP);
    if (e->phonetic && e->phonetic[0]) {
        x = ui_draw_text(t, f, x + 10, 1, "[", UI_BAR_RAMP);
        x = ui_draw_text(t, f, x, 1, e->phonetic, UI_BAR_RAMP);
        ui_draw_text(t, f, x, 1, "]", UI_BAR_RAMP);
    }

    y = UI_LINE_H + 3;
    draw_para(t, f, e->trans_zh, &y, UI_RAMP, -1);
    y += 4;
    draw_para(t, f, e->def_en, &y, UI_DIM_RAMP, UI_H - UI_LINE_H * 2);

    t->fill(t->ctx, 0, UI_H - UI_LINE_H, UI_W, UI_LINE_H, UI_BAR);
    ui_draw_text(t, f, 3, UI_H - UI_LINE_H + 1,
                 "\xe8\x8b\xb1\xe6\xbc\xa2  F1 \xe7\x99\xbc\xe9\x9f\xb3  "
                 "F2 \xe5\x88\x87\xe6\x8f\x9b  F3 \xe6\x94\xbe\xe5\xa4\xa7",
                 UI_BAR_RAMP);
}

void ui_render_typing(const ui_target *t, font *f, const char *typed,
                      const ui_cand *rows, int n)
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
        if (i == 0)
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
    ui_draw_text(t, f, 3, UI_H - UI_LINE_H + 1,
                 "\xe5\xb8\xb8\xe7\x94\xa8\xe8\xa9\x9e\xe5\x84\xaa\xe5\x85\x88"
                 "   ENTER \xe6\x9f\xa5\xe8\xa9\xa2", UI_BAR_RAMP);
}
