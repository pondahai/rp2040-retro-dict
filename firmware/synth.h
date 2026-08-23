/* 共振峰合成器。tools/synth/ 的 C 版。
 *
 * 中英文共用同一個核心 —— 差別只在怎麼「排」共振峰軌跡（見 U3-REPORT §7）。
 *
 * **定點運算**：RP2040 沒有 FPU。共振器係數是 Q12，狀態用 int32。
 * 係數含 exp/cos，但那些只在**參數變動時**算，不是每個取樣點都算 ——
 * 共振峰軌跡是平滑的，每 SYN_COEF_INTERVAL 個取樣點更新一次就夠。
 *
 * 沒有 malloc。輸出緩衝區由呼叫端提供。
 */
#ifndef SYNTH_H
#define SYNTH_H

#include <stdint.h>

#include "synth_tables.h"

#define SYN_COEF_INTERVAL 32     /* 每 32 個取樣點更新一次係數（2ms） */

typedef struct {
    int32_t y1, y2;
    int32_t a, b, c;             /* Q12 係數 */
} syn_res;

/* 一個音節／音素段的算好參數 */
typedef struct {
    uint16_t f[3];               /* 共振峰 Hz */
    uint16_t bw[3];              /* 頻寬 Hz */
    uint16_t f0_q8;              /* 基頻 Hz，Q8 */
    uint8_t  voiced;             /* 1 = 濁音源，0 = 噪音源 */
    uint8_t  silent;             /* 1 = 靜音（塞音成阻） */
} syn_frame;

/* 呼叫端每次要多少取樣點就給多少；回傳實際填了幾個。 */
typedef int (*syn_frame_fn)(void *ctx, syn_frame *out);

typedef struct {
    syn_res r1, r2, r3;          /* 共振峰 */
    syn_res glottal;             /* 聲門低通，F=0 */
    uint32_t rng;                /* 噪音用的 xorshift */
    int32_t phase_q16;           /* 脈衝源相位 */
    int32_t prev;                /* 嘴唇輻射的一階差分狀態 */
} syn_state;

void syn_init(syn_state *s, uint32_t seed);

/* 算一段波形。輸出是 **int32**，還沒正規化 —— 共振器的輸出本來就可能遠大於
 * int16，在正規化之前夾範圍會把波形削爛。 */
int syn_render(syn_state *s, const syn_frame *fr, int n,
               int32_t *out, int max_out);

/* 響度正規化 + 軟限幅，並轉成 int16。**四步缺一不可**，見 U3-REPORT §4.6：
 * 擦音經過高 Q 共振器後可以比濁音大 1700 倍，只做整體正規化會讓母音
 * 完全聽不見。 */
void syn_normalize(const int32_t *in, int n, int pre_len,
                   int quiet_consonant, int16_t *out);

/* --- 中文 --- */
/* 把一個音節 id（.DAT 的 SYL_ZH 內容）算成波形。
 * work 是呼叫端提供的暫存區，長度要 >= max_out。
 * 回傳取樣點數，或 -1 表示 id 無效。 */
int syn_syllable(syn_state *s, uint16_t syl_id,
                 int32_t *work, int16_t *out, int max_out);

/* --- 英文 --- */
/* 一個音素 id（.DAT 的 SYL_EN 內容）。 */
int syn_phoneme(syn_state *s, uint16_t ph_id,
                int32_t *work, int16_t *out, int max_out);

/* --- 工具 --- */
int syn_ms(int ms);              /* 毫秒 -> 取樣點數 */

#endif /* SYNTH_H */
