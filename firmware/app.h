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
#include "ime.h"
#include "keys.h"
#include "ui.h"

#define APP_MAX_TYPED 40
#define APP_MAX_CANDS 8
#define APP_MAX_WORD  32
#define APP_MAX_TRANS 256
#define APP_MAX_SYL   96          /* 48 個音素，比最長的英文詞還長 */

typedef enum {
    APP_TYPING = 0,
    APP_RESULT = 1
} app_state;

/* 查詢方向。Fn+2 切換。 */
typedef enum {
    APP_EC = 0,       /* 英漢：直接打字母 */
    APP_CE = 1        /* 漢英：注音輸入 */
} app_dir;

typedef struct {
    dict *d;          /* 目前方向用的字典（下面兩個之一） */
    dict *ec;
    dict *ce;         /* 沒有漢英資料時給 NULL，Fn+2 就不作用 */
    font *f;
    const ui_target *t;

    app_state state;
    app_dir dir;

    /* 注音輸入的狀態。只有 APP_CE 用得到。
     * typed 存的是**已經選定的中文字**，注音打到一半的部分放這裡 —— 兩者
     * 分開才知道退格要刪哪一邊。 */
    char ime_keys[IME_MAX_KEYS + 1];   /* 還沒選字的那串按鍵 */
    int  ime_len;
    char ime_cands[512];               /* 這串注音查到的候選字 */
    int  ime_n;                        /* 候選字有幾個 */
    char typed[APP_MAX_TYPED + 1];
    int  typed_len;

    int  sel;                     /* 候選清單反白第幾列 */
    int  scroll;                  /* 詞條內文捲到第幾行 */
    int  dirty;                   /* 有沒有需要重畫 */

    /* 候選清單 */
    ui_cand cands[APP_MAX_CANDS];
    char cand_word[APP_MAX_CANDS][APP_MAX_WORD];
    char cand_trans[APP_MAX_CANDS][APP_MAX_TRANS];
    uint32_t cand_off[APP_MAX_CANDS];   /* 候選詞在 .DAT 的位置，發音時才回頭讀 */
    uint16_t cand_len[APP_MAX_CANDS];
    int  cand_n;

    /* 目前顯示的詞條 */
    ui_entry entry;
    char hw[64], ph[128], zh[1024], en[2048];
    int  body_lines;
    uint8_t syl[APP_MAX_SYL];      /* 目前詞條的發音 id，原樣複製 */
    uint16_t syl_len;
    uint32_t idx_pos;              /* 目前詞條在主索引裡的序號 */
    int  has_pos;                  /* idx_pos 有效嗎（查不到時就沒有） */

    /* 右下角狀態格的兩個外部狀態。caps 由鍵盤層給（app 看不到 keys），
     * speaking 由板子端給（只有它知道 DMA 播完了沒）。 */
    int caps;
    int speaking;

    /* Fn+1 發音。這一層不合成、不碰喇叭 —— 只把「該唸什麼」交出去：
     * ids 是 .DAT 存好的音素／音節 id（FORMAT.md §4.2），沒有的話 ids 為 NULL，
     * 呼叫端就拿 fallback 逐字母唸。 */
    void (*speak)(void *ctx, const uint8_t *ids, int nbytes, int is_zh,
                  const char *fallback);
    void *speak_ctx;

    /* .DAT 記錄的暫存區。放在結構裡，這一層一樣不 malloc。 */
    uint8_t blob[4096];
} app;

/* ce 可以是 NULL（SD 卡上沒有漢英資料時），Fn+2 就不作用。 */
void app_init(app *a, dict *ec, dict *ce, font *f, const ui_target *t);
/* 餵一個按鍵事件。只改狀態，不畫 —— 一次掃描可能來好幾個事件。 */
void app_key(app *a, const key_event *ev);
/* 狀態列該顯示什麼（英漢／漢英／注音選字中）。 */
const char *app_bar(const app *a);

/* 右下角狀態格：模式與發音中的提示。
 * 「英」「英大」「注」互斥 —— 注音模式下大寫鎖定沒有意義。 */
const char *app_status(const app *a);

/* 需要時才重畫。回 1 表示這次真的畫了。 */
int  app_render(app *a);

#endif /* APP_H */
