// PWM 音訊輸出（GPIO 7，與 PicoApple2 / InfoNES 同一支腳）。
//
// 做法沿用 InfoNES 的 audio.c：PWM 的責任週期就是取樣值，後面接 RC 低通與
// PAM8403。差別在於這裡只要**一次放一整段**（按 Fn+1 唸一個字），不需要
// InfoNES 那種連續串流，所以不必用三段 DMA 鏈與中斷 —— 一個 DMA 通道就夠。
//
// 兩個關鍵設定：
//   PWM 載波跑 490kHz（wrap 254、clkdiv 1）—— 遠在聽覺之上，RC 濾掉就好。
//     若讓 PWM 直接跑在 16kHz 取樣率上，載波本身就會變成刺耳的高音。
//   取樣速率由 **DMA timer** 決定（125MHz x 2/15625 = 16000Hz），不是由 PWM。
//     這樣載波頻率與取樣率就完全解耦了。
#ifndef RETRODICT_AUDIO_H
#define RETRODICT_AUDIO_H

#include <stdint.h>

// 準備 PWM 與 DMA。只需呼叫一次。
void audio_init(int pin, int sample_rate);
// 播放一段 8-bit 無號 PCM（128 = 靜音）。非阻塞：DMA 播、CPU 繼續跑 UI。
// 緩衝區在播完之前不可以動，所以呼叫端要用不會被覆寫的那一塊。
void audio_play(const uint8_t *pcm, int n);
// 還在播嗎。
bool audio_busy(void);
// 停下來並回到靜音位準。
void audio_stop(void);

#endif
