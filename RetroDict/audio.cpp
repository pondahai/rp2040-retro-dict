#include "audio.h"

#include <Arduino.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"

static int s_slice = -1;
static int s_chan;
static int s_dma = -1;
static int s_timer = -1;

void audio_init(int pin, int sample_rate)
{
    if (s_slice >= 0)
        return;

    gpio_set_function(pin, GPIO_FUNC_PWM);
    s_slice = pwm_gpio_to_slice_num(pin);
    s_chan = pwm_gpio_to_channel(pin);

    // 載波：wrap 254、clkdiv 1 -> 125MHz / 255 = 490kHz。
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 1.0f);
    pwm_config_set_wrap(&cfg, 254);
    pwm_init(s_slice, &cfg, true);
    pwm_set_chan_level(s_slice, s_chan, 127);      // 靜音位準

    // 取樣速率由 DMA timer 決定：sys_clk * X / Y。125MHz x 2/15625 = 16000Hz。
    // X/Y 都是 16-bit，所以先把兩者一起約分到放得下為止。
    s_timer = dma_claim_unused_timer(true);
    {
        uint32_t sys = clock_get_hz(clk_sys);
        uint32_t x = 1, y = sys / (uint32_t)sample_rate;
        while (y > 0xFFFF) {                        // 放不下就同乘同除
            x <<= 1;
            y = (uint32_t)((uint64_t)sys * x / (uint32_t)sample_rate);
            if (x > 0xFFFF)
                break;
        }
        dma_timer_set_fraction(s_timer, (uint16_t)x, (uint16_t)y);
    }

    s_dma = dma_claim_unused_channel(true);
}

bool audio_busy(void)
{
    return s_dma >= 0 && dma_channel_is_busy(s_dma);
}

void audio_stop(void)
{
    if (s_dma < 0)
        return;
    dma_channel_abort(s_dma);
    pwm_set_chan_level(s_slice, s_chan, 127);
}

void audio_play(const uint8_t *pcm, int n)
{
    if (s_dma < 0 || !pcm || n <= 0)
        return;
    audio_stop();

    dma_channel_config c = dma_channel_get_default_config(s_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, dma_get_timer_dreq(s_timer));
    // cc 是 32 bit，A 在低半、B 在高半 —— 8-bit 寫入要落在對的那個 byte 上。
    dma_channel_configure(s_dma, &c,
                          (uint8_t *)&pwm_hw->slice[s_slice].cc + 2 * s_chan,
                          pcm, (uint32_t)n, true);
}
