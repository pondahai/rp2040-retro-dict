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
    /* 噪音源的振幅（Q8，256 = 基準）。**必須逐段設定**：voice.py 給爆破、
     * 送氣、擦音各自的振幅（0.5 / 0.28 / 0.38…），全部用同一個值等於把
     * 「爆破強、送氣弱」這層結構抹平。0 視同 256，讓沒設定的呼叫端維持原樣。 */
    uint16_t noise_q8;
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
/* cons_q8 與 quiet_consonant 是**兩件事**，不要合併：
 *   cons_q8         聲母段相對母音段的響度（Q8）。**一直都會套用**，
 *                   傳 0 會把整個聲母段乘成靜音。一般是
 *                   SYN_CONSONANT_LEVEL_Q8 * SYN_KIND_LEVEL_Q8[kind] / 256
 *                   —— 分類別是因為塞擦音的爆破段有 55ms、是塞音的 9 倍長，
 *                   同樣響度聽起來重得多。
 *   quiet_consonant 只在包絡那一步多壓 0.9，給噪音類的子音用。 */
void syn_normalize(const int32_t *in, int n, int pre_len,
                   int cons_q8, int quiet_consonant, int16_t *out);

/* --- 中文 --- */
/* 把一個音節 id（.DAT 的 SYL_ZH 內容）算成波形。
 * work 是呼叫端提供的暫存區，長度要 >= max_out。
 * 回傳取樣點數，或 -1 表示 id 無效。 */
int syn_syllable(syn_state *s, uint16_t syl_id,
                 int32_t *work, int16_t *out, int max_out);

/* prev_tone 的兩個特殊值。差別只在輕聲身上，但差很多：
 *   SYN_TONE_NONE  句首。輕聲不該出現在句首，真的出現就退回半上（三聲）。
 *   SYN_TONE_RAW   沒有脈絡這回事，照 SYN_TONE_CURVE[0] 原樣唸。
 * 分開是必要的：compare_synth.py 逐音節比對時，Python 端拿的就是原樣的
 * 聲調 0 曲線與時長，用 NONE 會變成 300ms 的半上、跟參考差 2880 個取樣點。 */
#define SYN_TONE_NONE (-1)
#define SYN_TONE_RAW  (-2)

/* 同上，但帶跨音節的脈絡：
 *   prev_tone  前一個字的調類（0..4），句首傳 SYN_TONE_NONE
 *   is_final   是不是整串的最後一個音節（要拉長 SYN_FINAL_LENGTHEN_PCT%）
 *
 * 為什麼要多一支：輕聲的實際音高取決於前字、句末音節要拉長 —— 這兩條都
 * 跨越單一音節，syn_syllable() 看不到。移植自 tools/synth/prosody.py 的
 * plan()，U3 聽判判定 07（看前字）比 08（一律等高）好，所以要做。 */
int syn_syllable_ctx(syn_state *s, uint16_t syl_id, int prev_tone, int is_final,
                     int32_t *work, int16_t *out, int max_out);

/* --- 英文 --- */
/* 一個音素 id（.DAT 的 SYL_EN 內容）。 */
int syn_phoneme(syn_state *s, uint16_t ph_id,
                int32_t *work, int16_t *out, int max_out);

/* 這個音素是不是母音（含雙母音）。句末降調要先數過整個詞的母音才算得出
 * 每個母音該降多少，所以呼叫端需要這支做前掃。 */
int syn_en_is_vowel(uint16_t ph_id);

/* 這個音素的基頻（Q8）。母音回傳「重音 × 降調」算完的值；非母音回 0，
 * 表示「我沒有自己的音高，沿用前一個母音的」—— 對應 Python 參考實作
 * english.py synth() 裡那個跨段延續的 last_f0。
 * decl_q8 是降調倍率，256 = 不降。 */
int syn_en_f0_q8(uint16_t ph_id, int decl_q8);

/* 句末降調的狀態機。
 *
 * Python 參考實作（english.py 的 plan()）是整個詞先攤成段清單再算，因為
 * 它有整個詞的 RAM 可用。板子上不行 —— speech.c 是一段合成完就送走的串流
 * 架構。所以這裡是等價的串流形式：先數出母音總數，之後每遇到一個母音就
 * 往下降一格，子音沿用前一個母音的音高。每個母音算出來的基頻與 Python
 * 逐一相同，只是計算的時機不同。
 *
 * 放在 synth.h 而不是 speech.c 裡面，是為了讓測試程式能走同一份程式碼 ——
 * 測試自己抄一份算式的話，兩邊一起改壞就驗不出來了。 */
typedef struct {
    int n_vowels;
    int seen;
    int carry_f0_q8;
    /* 跨音素平滑用：前一個音素結束時的共振峰。has_last=0 表示這是詞首，
     * 沒有東西可以滑進來。 */
    uint16_t last_f[3];
    int has_last;
    int smooth_ms;          /* 0 = 不平滑。預設 SYN_EN_SMOOTH_MS */
} syn_en_ctx;

void syn_en_ctx_init(syn_en_ctx *c, int n_vowels);

/* 改變之後 syn_en_ctx_init() 用的平滑視窗。只給測試程式做 A/B 用 ——
 * 韌體不呼叫它，所以預設值就是 SYN_EN_SMOOTH_MS。 */
void syn_en_set_smoothing(int ms);

/* 這個音素該用的基頻（Q8）。母音會順便把狀態往前推一格。 */
int syn_en_ctx_f0(syn_en_ctx *c, uint16_t ph_id);

/* 前掃：一串音素 id 裡有幾個母音。 */
int syn_en_count_vowels(const uint16_t *ids, int n);

/* 同 syn_phoneme()，但帶跨音素的脈絡（句末降調 + 共振峰平滑）。
 * ctx 傳 NULL 就等於 syn_phoneme()。ctx 會被就地更新。 */
int syn_phoneme_ctx(syn_state *s, uint16_t ph_id, syn_en_ctx *ctx,
                    int32_t *work, int16_t *out, int max_out);


/* --- 工具 --- */
int syn_ms(int ms);              /* 毫秒 -> 取樣點數 */

#endif /* SYNTH_H */
