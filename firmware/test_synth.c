/* 在 PC 上跑 C 版合成器，輸出 WAV。
 *
 *   test_synth <輸出.wav> zh  <音節id> [<音節id> ...]   逐音節，無脈絡
 *   test_synth <輸出.wav> en  <音素id> [<音素id> ...]
 *   test_synth <輸出.wav> zhw <音節id> [<音節id> ...]   整串，走 speech.c
 *   test_synth <輸出.wav> enw <音素id> [<音素id> ...]   整詞，走 speech.c
 *   test_synth <輸出.wav> enw0 <音素id> [...]           同上但關掉平滑（A/B 用）
 *   test_synth -          enf0 <音素id> [<音素id> ...]  只印每個音素的基頻
 *
 * enf0 不產生波形，印的是 syn_en_ctx 決定的 f0（Q8）—— 跟 speech.c 走的是
 * 同一份程式碼，不是照抄的第二份實作—— 句末降調是「每個
 * 母音該多高」的規則，直接比這串數字比從波形量音高準得多。
 *
 * zh 與 zhw 的差別就是這次移植的東西：zhw 會走 speech_ids()，於是有音節
 * 間隙、輕聲看前字、句末拉長；zh 沒有。兩個模式都留著，因為逐音節比對
 * 是驗合成器本身、整串比對是驗跨音節那一層，兩者驗的不是同一件事。
 *
 * id 就是 .DAT 裡 SYL_ZH / SYL_EN 存的 u16 —— 也就是說這支程式走的是
 * 韌體到時候完全一樣的路。firmware/compare_synth.py 會拿它跟 Python 版
 * 的波形逐點比對。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "speech.h"
#include "synth.h"

#define MAXSAMP 200000

static int16_t buf[MAXSAMP];
static int16_t one[MAXSAMP];
static int32_t work[MAXSAMP];

/* speech.c 的暫存區。跟板子用同一個上限（synth_tables.h 算出來的），
 * 免得 PC 上過了、板子上卻因為緩衝區比較小而被截掉。 */
#define MAXSEG SYN_MAX_SEG_SAMPLES
static int32_t sp_work[MAXSEG];
static int16_t sp_seg[MAXSEG];
static uint8_t sp_pcm[MAXSEG];
static int sp_total;

/* speech.c 交出來的是 8-bit 無號（128 = 靜音）。轉回 int16 只是為了
 * 沿用同一個 WAV 寫出器與 compare_synth.py 的比對函式；這一步是可逆的，
 * 不會再引入誤差。 */
static void collect(void *ctx, const uint8_t *pcm, int n)
{
    int i;
    (void)ctx;
    for (i = 0; i < n && sp_total < MAXSAMP; i++)
        buf[sp_total++] = (int16_t)(((int)pcm[i] - 128) << 8);
}

static void put32(FILE *f, uint32_t v)
{
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f);
    fputc((v >> 24) & 0xFF, f);
}

static void put16(FILE *f, uint16_t v)
{
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
}

static int write_wav(const char *path, const int16_t *s, int n)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + n * 2);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, SYN_SR);
    put32(f, SYN_SR * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, n * 2);
    fwrite(s, 2, n, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    syn_state st;
    int total = 0, i;
    int is_zh;

    if (argc < 4) {
        fprintf(stderr, "用法見檔頭註解\n");
        return 1;
    }
    is_zh = strcmp(argv[2], "zh") == 0;

    /* --- 只印基頻：驗句末降調 --- */
    if (strcmp(argv[2], "enf0") == 0) {
        syn_en_ctx probe;
        int nv = 0;
        for (i = 3; i < argc; i++)
            nv += syn_en_is_vowel((uint16_t)strtoul(argv[i], NULL, 10));
        syn_en_ctx_init(&probe, nv);
        for (i = 3; i < argc; i++) {
            uint16_t id = (uint16_t)strtoul(argv[i], NULL, 10);
            printf("%d\n", syn_en_ctx_f0(&probe, id));
        }
        return 0;
    }

    /* --- 整串模式：走韌體真正會走的 speech_ids() --- */
    if (strcmp(argv[2], "zhw") == 0 || strcmp(argv[2], "enw") == 0 ||
        strcmp(argv[2], "enw0") == 0) {
        int zh = strcmp(argv[2], "zhw") == 0;
        if (strcmp(argv[2], "enw0") == 0)
            syn_en_set_smoothing(0);
        static uint8_t ids[256];
        speech sp;
        int nb = 0;
        for (i = 3; i < argc && nb + 2 <= (int)sizeof(ids); i++) {
            uint16_t id = (uint16_t)strtoul(argv[i], NULL, 10);
            ids[nb++] = (uint8_t)(id & 0xFF);
            ids[nb++] = (uint8_t)(id >> 8);
        }
        sp_total = 0;
        speech_init(&sp, collect, NULL, sp_work, sp_seg, sp_pcm, MAXSEG);
        speech_ids(&sp, ids, nb, zh);
        if (write_wav(argv[1], buf, sp_total) != 0) {
            fprintf(stderr, "寫不了 %s\n", argv[1]);
            return 3;
        }
        printf("%d\n", sp_total);
        return 0;
    }

    for (i = 3; i < argc; i++) {
        uint16_t id = (uint16_t)strtoul(argv[i], NULL, 10);
        int n;
        /* 每個音節／音素各自從乾淨的狀態開始 —— 跟 Python 版一致 */
        syn_init(&st, 12345u);
        n = is_zh ? syn_syllable(&st, id, work, one, MAXSAMP)
                  : syn_phoneme(&st, id, work, one, MAXSAMP);
        if (n < 0) {
            fprintf(stderr, "id %u 無效\n", id);
            return 2;
        }
        if (total + n > MAXSAMP)
            n = MAXSAMP - total;
        memcpy(buf + total, one, n * 2);
        total += n;
    }

    if (write_wav(argv[1], buf, total) != 0) {
        fprintf(stderr, "寫不了 %s\n", argv[1]);
        return 3;
    }
    printf("%d\n", total);
    return 0;
}
