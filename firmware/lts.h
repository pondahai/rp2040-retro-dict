/* 英文字母 -> 音素（letter-to-sound）。
 *
 * **有音標就用音標**（`.DAT` 的 SYL_EN，轉檔期算好的）；沒有音標時才走這裡。
 * 使用者打到一半的字本來就不在字典裡，所以這一段必須在機器上即時跑，
 * 不能靠轉檔期先算 —— 這也是它為什麼會出現在韌體裡。
 *
 * 規格是 `tools/synth/lts.py`，規則表 `lts_tables.h` 由 `tools/gen_lts_tables.py`
 * 從那一份產生，兩邊由 `firmware/compare_lts.py` 逐字比對。
 *
 * **這不是正確的發音器，是「唸得出來」的發音器。** 對象是本來完全唸不出來
 * 的那七成詞條，基準線是逐字母拼，不是母語者發音。
 *
 * 沒有 malloc、沒有浮點數、沒有靜態狀態。
 */
#ifndef LTS_H
#define LTS_H

#include <stdint.h>

#define LTS_MAX_WORD         48   /* 單字超過就截斷 */
#define LTS_MAX_PH_PER_WORD  64

/* 一個單字 -> 音素序號（還沒加重音）。回傳個數。 */
int lts_word(const char *word, int wlen, uint8_t *out, int max_ph);

/* 整串（可含空白與連字號）-> 音素 id 陣列，可直接餵給 speech_ids()。
 * 每個單字各自把重音放在第一個母音上。回傳個數。 */
int lts_to_ids(const char *text, uint16_t *out, int max);

#endif /* LTS_H */
