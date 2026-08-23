/* 在 PC 上跑 C 版合成器，輸出 WAV。
 *
 *   test_synth <輸出.wav> zh <音節id> [<音節id> ...]
 *   test_synth <輸出.wav> en <音素id> [<音素id> ...]
 *
 * id 就是 .DAT 裡 SYL_ZH / SYL_EN 存的 u16 —— 也就是說這支程式走的是
 * 韌體到時候完全一樣的路。firmware/compare_synth.py 會拿它跟 Python 版
 * 的波形逐點比對。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "synth.h"

#define MAXSAMP 200000

static int16_t buf[MAXSAMP];
static int16_t one[MAXSAMP];
static int32_t work[MAXSAMP];

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
