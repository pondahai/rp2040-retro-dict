/* 在 PC 上驗證 dict.c —— 不需要板子、不需要 SD 卡。
 *
 * 這就是 PLAN.md §4「後台可在 PC 上測」的兌現：後台只依賴一個讀扇區的
 * 函式指標，所以這裡餵檔案就好。
 *
 * 輸出是 TSV，由 firmware/compare.py 拿去跟 Python 參考實作逐筆比對。
 *
 *   test_compare <DICT目錄> lookup <正規化後的鍵>
 *   test_compare <DICT目錄> prefix <鍵> <limit> <window> <0|1 common_first>
 *   test_compare <DICT目錄> selftest
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dict.h"

typedef struct {
    FILE *f;
} file_ctx;

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

static int file_read_dat(void *ctx, uint32_t off, uint32_t len, uint8_t *out)
{
    file_ctx *fc = (file_ctx *)ctx;
    if (fseek(fc->f, (long)off, SEEK_SET) != 0)
        return -1;
    return fread(out, 1, len, fc->f) == len ? 0 : -1;
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

static FILE *try_open(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    return fopen(path, "rb");
}

static file_ctx g_idx, g_dat, g_com;

static int setup(dict *d, const char *dir, const char *base)
{
    char idx[64], dat[64], com[64];
    FILE *cf;
    snprintf(idx, sizeof(idx), "%s.IDX", base);
    snprintf(dat, sizeof(dat), "%s.DAT", base);
    snprintf(com, sizeof(com), "%sC.IDX", base);

    g_idx.f = open_or_die(dir, idx);
    g_dat.f = open_or_die(dir, dat);
    if (dict_index_open(&d->main, file_read_sector, &g_idx) != DICT_OK) {
        fprintf(stderr, "索引檔頭壞了\n");
        return -1;
    }
    d->read_dat = file_read_dat;
    d->dat_ctx = &g_dat;

    cf = try_open(dir, com);
    d->has_common = 0;
    if (cf) {
        g_com.f = cf;
        if (dict_index_open(&d->common, file_read_sector, &g_com) == DICT_OK)
            d->has_common = 1;
    }
    return 0;
}

static void print_escaped(const uint8_t *p, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (p[i] == '\t' || p[i] == '\n' || p[i] == '\r')
            printf(" ");
        else
            printf("%c", p[i]);
    }
}

int main(int argc, char **argv)
{
    dict d;
    const char *dir, *cmd;
    static uint8_t buf[70000];

    if (argc < 3) {
        fprintf(stderr, "用法見檔頭註解\n");
        return 1;
    }
    dir = argv[1];
    cmd = argv[2];
    memset(&d, 0, sizeof(d));

    if (strcmp(cmd, "selftest") == 0) {
        /* 正規化必須與 Python 一致 —— 不一致會靜默失敗 */
        struct { const char *in; const char *want; } cases[] = {
            { "  Hello   World  ", "hello world" },
            { "re-do it",          "re-do it" },
            { "!!!",               "" },
            { "A  B",              "a b" },
            { "TRAILING   ",       "trailing" },
        };
        int i, bad = 0;
        uint8_t out[64];
        for (i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
            uint32_t n = dict_normalize_ec(cases[i].in, out, sizeof(out));
            out[n] = 0;
            if (strcmp((char *)out, cases[i].want) != 0) {
                printf("FAIL\tnormalize\t%s\t得到 %s\t應為 %s\n",
                       cases[i].in, out, cases[i].want);
                bad++;
            }
        }
        printf("normalize\t%d 項\t%d 失敗\n",
               (int)(sizeof(cases) / sizeof(cases[0])), bad);
        return bad ? 1 : 0;
    }

    {
        const char *base = (dir[0] && strstr(cmd, "ce")) ? "CE" : "EC";
        if (argc > 6 && strcmp(argv[6], "ce") == 0)
            base = "CE";
        if (getenv("DICT_BASE"))
            base = getenv("DICT_BASE");
        if (setup(&d, dir, base) != 0)
            return 2;
    }

    if (strcmp(cmd, "lookup") == 0 || strcmp(cmd, "lookuphex") == 0) {
        dict_record rec;
        dict_cursor cur;
        static uint8_t keybuf[DICT_MAX_KEY + 1];
        const uint8_t *key = (const uint8_t *)argv[3];
        uint32_t klen = (uint32_t)strlen(argv[3]);
        int n, hits = 0;
        /* 中文的鍵是 UTF-8，透過命令列傳會被字碼頁轉換弄壞，所以另外
         * 提供十六進位的傳法。漢英方向如果不測，等於一半的字典沒驗到。 */
        if (strcmp(cmd, "lookuphex") == 0) {
            uint32_t i;
            klen = (uint32_t)strlen(argv[3]) / 2;
            if (klen > DICT_MAX_KEY)
                klen = DICT_MAX_KEY;
            for (i = 0; i < klen; i++) {
                unsigned v = 0;
                sscanf(argv[3] + i * 2, "%2x", &v);
                keybuf[i] = (uint8_t)v;
            }
            key = keybuf;
        }
        d.main.src.reads = 0;
        /* 同一個鍵可能有多筆詞條（多音字、同形異義），要全部走完 —— 也只有
         * 走完才知道總共讀了幾個扇區，那個數字要跟 Python 對得起來。 */
        n = dict_lookup_first(&d, key, klen, &cur, buf, sizeof(buf), &rec);
        while (n == 1) {
            uint16_t flen = 0, tl = 0;
            const uint8_t *hw = dict_field(&rec, DICT_T_HEADWORD, &flen);
            const uint8_t *tr;
            printf("HIT\t%u\t", rec.rank);
            if (hw)
                print_escaped(hw, flen);
            printf("\t");
            tr = dict_field(&rec, DICT_T_TRANS_ZH, &tl);
            if (!tr)
                tr = dict_field(&rec, DICT_T_DEF_EN, &tl);
            if (tr)
                print_escaped(tr, tl);
            printf("\n");
            hits++;
            n = dict_lookup_next(&d, &cur, buf, sizeof(buf), &rec);
        }
        printf("END\t%d\t%u\n", hits, d.main.src.reads);
        return 0;
    }

    if (strcmp(cmd, "prefix") == 0) {
        dict_entry out[64];
        int limit = atoi(argv[4]);
        int window = atoi(argv[5]);
        int cf = atoi(argv[6]);
        int n, i;
        if (limit > 64)
            limit = 64;
        n = dict_prefix(&d, (const uint8_t *)argv[3],
                        (uint32_t)strlen(argv[3]), out, limit, window, cf);
        for (i = 0; i < n; i++) {
            uint32_t kl = 0;
            while (kl < DICT_KEY24 && out[i].key24[kl])
                kl++;
            print_escaped(out[i].key24, kl);
            printf("\t%u\n", out[i].rank);
        }
        return 0;
    }

    fprintf(stderr, "不認識的命令 %s\n", cmd);
    return 1;
}
