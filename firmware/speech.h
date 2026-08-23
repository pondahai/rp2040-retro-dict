/* 發音：把 .DAT 裡存好的音素／音節 id 變成 8-bit PCM。
 *
 * 跟其他層一樣不知道硬體存在 —— 波形一段一段交給呼叫端給的 sink，
 * 板子上把它推進 PWM 的 DMA 緩衝，PC 上寫成 WAV。所以「唸出來對不對」
 * 這件事在 PC 上就能用耳朵驗，不必燒板子。
 *
 * **這一層沒有 g2p**（FORMAT.md §4.2）：英文音素與中文音節在轉檔期就算好了，
 * 韌體只是把一串 u16 餵給合成器。唯一的例外是 speech_spell()，它逐字母唸，
 * 用的也是同一套音素表（spell_tables.h 由 tools/synth/phoneme.py 產生）。
 *
 * 沒有 malloc：暫存區由呼叫端提供，大小決定「單一音素／音節」的長度上限。
 */
#ifndef SPEECH_H
#define SPEECH_H

#include <stdint.h>

#include "synth.h"

/* 一段波形。n 是取樣點數，8-bit 無號（128 = 靜音），SYN_SR Hz。 */
typedef void (*speech_sink)(void *ctx, const uint8_t *pcm, int n);

typedef struct {
    syn_state st;
    speech_sink sink;
    void *ctx;
    int32_t *work;          /* 呼叫端提供，長度 max_seg */
    int16_t *seg;           /* 同上 */
    uint8_t *pcm;           /* 同上（8-bit，長度 max_seg） */
    int max_seg;
    int samples;            /* 這次唸了幾個取樣點，供測試用 */
} speech;

void speech_init(speech *sp, speech_sink sink, void *ctx,
                 int32_t *work, int16_t *seg, uint8_t *pcm, int max_seg);

/* 唸一串 id。ids 是 .DAT 的 SYL_EN / SYL_ZH 欄位原樣（u16 小端序）。
 * 回傳取樣點總數，負值為錯誤。 */
int speech_ids(speech *sp, const uint8_t *ids, int nbytes, int is_zh);

/* 逐字母唸（a-p-p）。查無此字時用它，這也是 1980 年代電子字典的行為。
 * 只認 ASCII 字母與數字，其餘跳過。回傳取樣點總數。 */
int speech_spell(speech *sp, const char *ascii);

#endif /* SPEECH_H */
