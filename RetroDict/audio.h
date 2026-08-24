/* PWM 音訊輸出。**原封取自 rp2040-ili9341-infones 的 software/infones/audio.c
 * 與 audio.h**（同一塊板子上已經驗證會響的那一份），只加了這段說明。
 *
 * 為什麼不自己寫一份小的：第一版是自己寫的 —— PWM 載波 490kHz、取樣速率
 * 用 DMA timer 給、8-bit DMA 直接寫進 PWM 的 cc 暫存器。結果實機只有卡答聲，
 * 連 1kHz 方波測試音也一樣。InfoNES 這份繞了一圈（先把 8-bit 樣本寫進 RAM 的
 * single_sample，再用 **32-bit** DMA 把整個 word 搬進 cc，並用 REPETITION_RATE
 * 讓 PWM 跑在取樣率的 4 倍）—— 那個繞法很可能正是為了避開窄寬度寫周邊暫存器
 * 的問題。既然板子上已經有一份會響的，就用它。
 *
 * 後來動過的地方：AUDIO_BUFFER_SIZE（見下）、以及 audio.c 的 REPETITION_RATE
 * （4 -> 16，把 PWM 載波從 64kHz 拉到 256kHz，理由寫在 audio.c）。
 *
 * 注意：audio_mixer_step() 要在主迴圈裡定期呼叫，緩衝區是 1024 個樣本
 * （16kHz 下 64ms），久久不呼叫就會斷音。
 */
#ifndef AUDIO_H_FILE
#define AUDIO_H_FILE

// 原本是 1024（16kHz 下 64ms）。字典跟 InfoNES 不一樣：重畫一張畫面要從
// SD 讀上百次字模，主迴圈一次卡 100ms 以上是常態，64ms 的緩衝區會來不及
// 填，ISR 照樣換緩衝區 -> 舊的那塊再播一次，聽起來就是 "aaaaple"。
#define AUDIO_BUFFER_SIZE 4096
#define AUDIO_MAX_SOURCES 2

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_init(int audio_pin, int sample_freq);
uint8_t *audio_get_buffer(void);

int audio_play_once(const uint8_t *samples, int len);
int audio_play_loop(const uint8_t *samples, int len, int loop_start);

void audio_source_stop(int source_id);
void audio_source_set_volume(int source_id, uint16_t volume);

void audio_mixer_step(void);
bool audio_is_source_active(int source_id);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H_FILE */
