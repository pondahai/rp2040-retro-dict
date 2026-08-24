/* 在 PC 上聽發音 —— 走的是板子上完全一樣的路：查字典拿 SYL_EN，
 * 餵給 speech.c，只有最後一步「波形往哪去」換成寫 WAV。
 *
 *   test_speak <DICT目錄> word <單字> <輸出.wav>    查字典唸（有音素資料才行）
 *   test_speak <DICT目錄> spell <字串> <輸出.wav>   逐字母唸
 *   test_speak <DICT目錄> letters <字串> <輸出.wav> 用字母規則推（lts.c）
 *   test_speak <DICT目錄> auto <字串> <輸出.wav>    查得到就唸，查不到就逐字母
 *
 * auto 就是 app.c 在輸入畫面按 Fn+1 的行為。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dict.h"
#include "speech.h"

#define MAXSAMP 400000
/* 單一音素／音節的長度上限。板子上是 SYN_MAX_SEG_SAMPLES，這裡取一樣的
 * 值 —— 這支測試程式比板子寬鬆的話，截斷的 bug 就只會在板子上出現。 */
#define MAXSEG  SYN_MAX_SEG_SAMPLES

typedef struct { FILE *f; } file_ctx;

static int file_read_sector(void *ctx, uint32_t sector, uint8_t *out)
{
    file_ctx *fc = (file_ctx *)ctx;
    size_t got;
    if (fseek(fc->f, (long)sector * DICT_SECTOR, SEEK_SET) != 0)
        return -1;
    got = fread(out, 1, DICT_SECTOR, fc->f);
    if (got < DICT_SECTOR)
        memset(out + got, 0, DICT_SECTOR - got);
    return 0;
}

static int file_read_at(void *ctx, uint32_t off, uint32_t len, uint8_t *out)
{
    file_ctx *fc = (file_ctx *)ctx;
    size_t got;
    if (fseek(fc->f, (long)off, SEEK_SET) != 0)
        return -1;
    got = fread(out, 1, len, fc->f);
    if (got < len)
        memset(out + got, 0, len - got);
    return 0;
}

static FILE *open_or_die(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "開不了 %s\n", path);
        exit(2);
    }
    return f;
}

/* ---- 收集波形 ---- */

static uint8_t PCM[MAXSAMP];
static int PCM_N;

static void sink(void *ctx, const uint8_t *pcm, int n)
{
    (void)ctx;
    if (PCM_N + n > MAXSAMP)
        n = MAXSAMP - PCM_N;
    memcpy(PCM + PCM_N, pcm, (size_t)n);
    PCM_N += n;
}

static void put32(FILE *f, uint32_t v)
{
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f); fputc((v >> 24) & 0xFF, f);
}

static void put16(FILE *f, uint16_t v)
{
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
}

/* 寫 8-bit WAV —— 刻意不是 16-bit：板子上送進 PWM 的就是這 8 bit，
 * 所以這個檔案聽到的就是喇叭會發出的東西，包含量化雜訊。 */
static int write_wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + PCM_N);
    fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, SYN_SR);
    put32(f, SYN_SR);
    put16(f, 1);
    put16(f, 8);
    fwrite("data", 1, 4, f);
    put32(f, (uint32_t)PCM_N);
    fwrite(PCM, 1, (size_t)PCM_N, f);
    fclose(f);
    return 0;
}

static int32_t WORK[MAXSEG];
static int16_t SEG[MAXSEG];
static uint8_t OUT[MAXSEG];

int main(int argc, char **argv)
{
    FILE *fidx, *fdat;
    static file_ctx cidx, cdat;
    static dict d;
    speech sp;
    uint8_t blob[8192];
    const char *dir, *mode, *arg, *out;
    int spelled = 0;

    if (argc < 5) {
        fprintf(stderr, "用法：test_speak <DICT目錄> word|spell|auto <字> <out.wav>\n");
        return 1;
    }
    dir = argv[1]; mode = argv[2]; arg = argv[3]; out = argv[4];

    fidx = open_or_die(dir, "EC.IDX");
    fdat = open_or_die(dir, "EC.DAT");
    cidx.f = fidx; cdat.f = fdat;
    memset(&d, 0, sizeof(d));
    if (dict_index_open(&d.main, file_read_sector, &cidx) != DICT_OK) {
        fprintf(stderr, "EC.IDX 開檔失敗\n");
        return 2;
    }
    d.read_dat = file_read_at;
    d.dat_ctx = &cdat;

    speech_init(&sp, sink, NULL, WORK, SEG, OUT, MAXSEG);

    if (strcmp(mode, "spell") == 0) {
        speech_spell(&sp, arg);
        spelled = 1;
    } else if (strcmp(mode, "letters") == 0) {
        if (speech_letters(&sp, arg) <= 0) {
            speech_spell(&sp, arg);
            spelled = 1;
        }
    } else {
        uint8_t key[DICT_MAX_KEY];
        uint32_t klen = dict_normalize_ec(arg, key, sizeof(key));
        dict_record rec;
        const uint8_t *syl = NULL;
        uint16_t slen = 0;

        if (dict_lookup(&d, key, klen, blob, sizeof(blob), &rec) == 1)
            syl = dict_field(&rec, DICT_T_SYL_EN, &slen);
        if (syl && slen >= 2) {
            speech_ids(&sp, syl, slen, 0);
            printf("查到「%s」，%d 個音素\n", arg, slen / 2);
        } else if (strcmp(mode, "auto") == 0) {
            /* 板子上的順序：有音標唸音標、沒有就用字母規則、再不行才逐字母 */
            if (speech_letters(&sp, arg) <= 0) {
                speech_spell(&sp, arg);
                spelled = 1;
            }
        } else {
            fprintf(stderr, "「%s」查不到或沒有音素資料\n", arg);
            return 3;
        }
    }

    if (write_wav(out) != 0) {
        fprintf(stderr, "寫不了 %s\n", out);
        return 2;
    }
    printf("%s\t%d 取樣點（%.2f 秒）%s\n", out, PCM_N,
           (double)PCM_N / SYN_SR, spelled ? "\t逐字母" : "");
    return 0;
}
