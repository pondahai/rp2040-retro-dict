/* 字典的前景：按鍵事件 -> 畫面。
 *
 * 這一層仍然不知道硬體存在。輸入是 keys.h 的事件，輸出是往 ui_target 畫，
 * 字典資料從 dict.h 來。板子上的主迴圈只做三件事：掃矩陣、餵事件、
 * 把畫布送上螢幕 —— 狀態機本身在 PC 上就能整段跑完（見 test_app.c）。
 *
 * 兩個畫面：
 *   APP_TYPING  邊打邊查，上面是輸入框，下面是常用詞優先的候選清單
 *   APP_RESULT  詞條內文，可捲動
 */
#ifndef APP_H
#define APP_H

#include "dict.h"
#include "font.h"
#include "keys.h"
#include "ui.h"

#define APP_MAX_TYPED 40
#define APP_MAX_CANDS 8
#define APP_MAX_WORD  32
#define APP_MAX_TRANS 256

typedef enum {
    APP_TYPING = 0,
    APP_RESULT = 1
} app_state;

typedef struct {
    dict *d;
    font *f;
    const ui_target *t;

    app_state state;
    char typed[APP_MAX_TYPED + 1];
    int  typed_len;

    int  sel;                     /* 候選清單反白第幾列 */
    int  scroll;                  /* 詞條內文捲到第幾行 */
    int  dirty;                   /* 有沒有需要重畫 */

    /* 候選清單 */
    ui_cand cands[APP_MAX_CANDS];
    char cand_word[APP_MAX_CANDS][APP_MAX_WORD];
    char cand_trans[APP_MAX_CANDS][APP_MAX_TRANS];
    int  cand_n;

    /* 目前顯示的詞條 */
    ui_entry entry;
    char hw[64], ph[128], zh[1024], en[2048];
    int  body_lines;

    /* F1 發音。接上 synth.h 之前先留掛勾，PC 端測試也靠它看有沒有被呼叫。 */
    void (*speak)(void *ctx, const char *word, const char *phonetic);
    void *speak_ctx;

    /* .DAT 記錄的暫存區。放在結構裡，這一層一樣不 malloc。 */
    uint8_t blob[4096];
} app;

void app_init(app *a, dict *d, font *f, const ui_target *t);
/* 餵一個按鍵事件。只改狀態，不畫 —— 一次掃描可能來好幾個事件。 */
void app_key(app *a, const key_event *ev);
/* 需要時才重畫。回 1 表示這次真的畫了。 */
int  app_render(app *a);

#endif /* APP_H */
