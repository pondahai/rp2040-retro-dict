/* 4bpp 調色盤畫布 —— UI 與螢幕之間的那一層。
 *
 * 為什麼不是 RGB565 全畫面：320x240x2 = 150 KB，RP2040 只有 264 KB，
 * 剩下的要塞 FatFs、合成器與堆疊。但整個 UI 只用到十來種顏色（三組四階
 * 灰 + 兩個列底色），所以存成 4 bit 索引 = **37.5 KB**，送螢幕時再逐列
 * 展開成 RGB565。省下來的 112 KB 比任何一種分區重繪都划算，而且 ui.c
 * 只要畫一次。
 *
 * 這一層是純 C，PC 與板子共用 —— `firmware/compare_ui.py` 的逐像素比對
 * 就是走這條路，所以打包／展開的正確性跟排版一起被驗證。
 */
#ifndef FBUF_H
#define FBUF_H

#include <stdint.h>

#include "ui.h"

#define FB_W        UI_W
#define FB_H        UI_H
#define FB_COLORS   16
#define FB_BYTES    (FB_W * FB_H / 2)

typedef struct {
    uint8_t  px[FB_BYTES];        /* 每 byte 兩個像素，高位在左 */
    uint32_t pal[FB_COLORS];      /* 索引 -> 0xRRGGBB */
    uint8_t  pal_n;
    uint8_t  overflow;            /* 顏色超過 16 種就記一筆，不靜靜畫錯 */
} fbuf;

void fbuf_init(fbuf *fb);
/* 填好 ui_target，之後 ui_render_* 直接畫進 fb。 */
void fbuf_target(fbuf *fb, ui_target *t);
uint32_t fbuf_get(const fbuf *fb, int x, int y);
/* 取一列，展開成 RGB565 big-endian（ILI9341 的線序）。 */
void fbuf_line_rgb565(const fbuf *fb, int y, uint8_t *out);

#endif /* FBUF_H */
