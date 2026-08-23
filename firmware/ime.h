/* 注音輸入法。引擎搬自 pico_keyboard_ime_terminal，改寫成純 C。
 *
 * 三步：按鍵 -> 注音符號 -> 加聲調成查詢鍵 -> 在排序索引上二分搜尋。
 * 碼表與鍵位對照都在 `ime_tables.h`，由 `tools/gen_ime_tables.py` 從上游
 * 解析產生（不是手抄 —— 那 41 組對照手抄一定會錯一兩個）。
 *
 * 一聲不用打；打了 3/4/6/7 就是二/三/四/輕聲（大千配列）。
 *
 * 沒有 malloc、沒有靜態狀態。表在 flash 裡（約 65KB），查詢只碰幾十個 byte。
 */
#ifndef IME_H
#define IME_H

#include <stdint.h>
#include <stddef.h>

#define IME_MAX_KEYS   12       /* 一個音節最多打幾個鍵 */
#define IME_MAX_BOPO   32       /* 注音符號串的 UTF-8 長度上限 */

/* 一個按鍵 -> 注音符號（UTF-8，NUL 結尾）。不是注音鍵回 NULL。 */
const char *ime_key_bopo(char key);

/* 一串按鍵 -> 注音符號串（顯示用，例如 "ㄋㄧˇ"）。回傳寫入的 byte 數。 */
int ime_bopomofo(const char *keys, char *out, int out_size);

/* 查候選字。keys 是使用者打的那串鍵；out 收 UTF-8 的候選字串（多個字接在
 * 一起，呼叫端自己切）。回傳 byte 數，0 = 查不到。 */
int ime_query(const char *keys, char *out, int out_size);

/* 候選字串裡的第 n 個字（UTF-8 逐字切）。回傳該字的 byte 數，0 = 沒有。 */
int ime_nth(const char *cands, int n, char *out, int out_size);

#endif /* IME_H */
