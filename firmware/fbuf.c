#include "fbuf.h"

#include <string.h>

void fbuf_init(fbuf *fb)
{
    memset(fb, 0, sizeof(*fb));
}

/* 顏色是用到才進調色盤。UI 的顏色是編譯期常數，所以這張表最後一定是
 * 那十來種，不會長大 —— 但還是留 overflow 旗標，寧可事後查得到。 */
static int index_of(fbuf *fb, uint32_t color)
{
    int i;
    for (i = 0; i < fb->pal_n; i++)
        if (fb->pal[i] == color)
            return i;
    if (fb->pal_n >= FB_COLORS) {
        fb->overflow = 1;
        return 0;
    }
    fb->pal[fb->pal_n] = color;
    return fb->pal_n++;
}

static void put(fbuf *fb, int x, int y, int idx)
{
    uint32_t o = (uint32_t)y * (FB_W / 2) + (uint32_t)(x >> 1);
    if (x & 1)
        fb->px[o] = (uint8_t)((fb->px[o] & 0xF0) | idx);
    else
        fb->px[o] = (uint8_t)((fb->px[o] & 0x0F) | (idx << 4));
}

static void t_pixel(void *ctx, int x, int y, uint32_t color)
{
    fbuf *fb = (fbuf *)ctx;
    if (x < 0 || y < 0 || x >= FB_W || y >= FB_H)
        return;
    put(fb, x, y, index_of(fb, color));
}

static void t_fill(void *ctx, int x, int y, int w, int h, uint32_t color)
{
    fbuf *fb = (fbuf *)ctx;
    int idx = index_of(fb, color);
    int i, j;
    for (j = y; j < y + h; j++) {
        if (j < 0 || j >= FB_H)
            continue;
        for (i = x; i < x + w; i++) {
            if (i < 0 || i >= FB_W)
                continue;
            put(fb, i, j, idx);
        }
    }
}

void fbuf_target(fbuf *fb, ui_target *t)
{
    t->ctx = fb;
    t->pixel = t_pixel;
    t->fill = t_fill;
}

uint32_t fbuf_get(const fbuf *fb, int x, int y)
{
    uint32_t o = (uint32_t)y * (FB_W / 2) + (uint32_t)(x >> 1);
    uint8_t b = fb->px[o];
    return fb->pal[(x & 1) ? (b & 0x0F) : (b >> 4)];
}

void fbuf_line_rgb565(const fbuf *fb, int y, uint8_t *out)
{
    uint16_t pal565[FB_COLORS];
    int i, x;
    for (i = 0; i < FB_COLORS; i++) {
        uint32_t c = fb->pal[i];
        pal565[i] = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) |
                               ((c >> 3) & 0x001F));
    }
    for (x = 0; x < FB_W; x++) {
        uint8_t b = fb->px[(uint32_t)y * (FB_W / 2) + (uint32_t)(x >> 1)];
        uint16_t v = pal565[(x & 1) ? (b & 0x0F) : (b >> 4)];
        *out++ = (uint8_t)(v >> 8);
        *out++ = (uint8_t)v;
    }
}
