/* 在 PC 上把 UI 畫出來 —— 不需要板子、不需要螢幕。
 *
 * 與 test_compare.c 同一個路數：字典走 dict.c、字模走 font.c、排版走 ui.c，
 * 只有最後一步「像素往哪去」換成寫 PPM。firmware/compare_ui.py 拿這張圖
 * 跟 tools/ui_preview.py 的 PIL 版本逐像素比對。
 *
 *   test_ui <DICT目錄> result <詞> <輸出.ppm> [捲動行數]
 *   test_ui <DICT目錄> typing <已打的字> <輸出.ppm> [反白第幾列]
 *
 * 畫面走的是板子上同一塊 4bpp 調色盤畫布（fbuf.c），所以連打包／展開
 * 都被這條比對線驗證，不只是排版。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dict.h"
#include "fbuf.h"
#include "font.h"
#include "ui.h"

/* ---- 檔案當成 SD ---- */

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
        memset(out + got, 0, len - got);   /* 字模檔尾端那個不滿的區塊 */
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

/* ---- 畫布 ---- */

static fbuf FB;

static int write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int x, y;
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", UI_W, UI_H);
    for (y = 0; y < UI_H; y++)
        for (x = 0; x < UI_W; x++) {
            uint32_t c = fbuf_get(&FB, x, y);
            uint8_t rgb[3];
            rgb[0] = (uint8_t)(c >> 16);
            rgb[1] = (uint8_t)(c >> 8);
            rgb[2] = (uint8_t)c;
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    return 0;
}

/* ---- 取欄位，補上結尾的 0 ---- */

static const char *field(const dict_record *rec, uint8_t tag, char *buf,
                         size_t size)
{
    uint16_t len;
    const uint8_t *p = dict_field(rec, tag, &len);
    if (!p)
        return NULL;
    if (len > size - 1)
        len = (uint16_t)(size - 1);
    memcpy(buf, p, len);
    buf[len] = 0;
    return buf;
}

int main(int argc, char **argv)
{
    const char *dir, *mode, *arg, *out;
    FILE *fidx, *fdat, *fcommon, *ffont;
    file_ctx cidx, cdat, ccommon, cfont;
    dict d;
    font fnt;
    ui_target t;
    uint8_t blob[8192];
    int rc, extra = 0;

    if (argc < 5) {
        fprintf(stderr, "用法：test_ui <DICT目錄> result|typing <字> <out.ppm>\n");
        return 1;
    }
    dir = argv[1]; mode = argv[2]; arg = argv[3]; out = argv[4];
    if (argc > 5)
        extra = atoi(argv[5]);

    fidx = open_or_die(dir, "EC.IDX");
    fdat = open_or_die(dir, "EC.DAT");
    fcommon = open_or_die(dir, "ECC.IDX");
    ffont = open_or_die(dir, "FONT.BIN");
    cidx.f = fidx; cdat.f = fdat; ccommon.f = fcommon; cfont.f = ffont;

    memset(&d, 0, sizeof(d));
    if ((rc = dict_index_open(&d.main, file_read_sector, &cidx)) != DICT_OK) {
        fprintf(stderr, "EC.IDX 開檔失敗 %d\n", rc);
        return 2;
    }
    if (dict_index_open(&d.common, file_read_sector, &ccommon) == DICT_OK)
        d.has_common = 1;
    d.read_dat = file_read_at;
    d.dat_ctx = &cdat;

    if ((rc = font_open(&fnt, file_read_at, &cfont)) != FONT_OK) {
        fprintf(stderr, "FONT.BIN 開檔失敗 %d\n", rc);
        return 2;
    }

    {
        /* 板子上也是這樣掛的（RetroDict.ino）。不掛的話光是碼位表的二分搜尋
         * 就要每個字十幾次 SD 讀取 —— 這裡印出來的 font_reads 就是那個成本。 */
        static uint8_t cache[64 * 1024];
        uint32_t want = font_cache_size(&fnt, 1);
        uint32_t got = font_cache(&fnt, cache, sizeof(cache));
        fprintf(stderr, "字模快取：掛上 %u / 想要 %u bytes%s\n",
                (unsigned)got, (unsigned)want,
                got ? (fnt.has_ascii ? "（含 ASCII 字模）" : "（只有索引）")
                    : "（沒掛上）");
    }

    fbuf_init(&FB);
    fbuf_target(&FB, &t);

    if (strcmp(mode, "result") == 0) {
        uint8_t key[DICT_MAX_KEY];
        uint32_t klen = dict_normalize_ec(arg, key, sizeof(key));
        dict_record rec;
        ui_entry e;
        char hw[256], ph[256], zh[2048], en[4096];

        rc = dict_lookup(&d, key, klen, blob, sizeof(blob), &rec);
        if (rc != 1) {
            fprintf(stderr, "查不到 %s（rc=%d）\n", arg, rc);
            return 3;
        }
        memset(&e, 0, sizeof(e));
        e.headword = field(&rec, DICT_T_HEADWORD, hw, sizeof(hw));
        e.phonetic = field(&rec, DICT_T_PHONETIC, ph, sizeof(ph));
        e.trans_zh = field(&rec, DICT_T_TRANS_ZH, zh, sizeof(zh));
        e.def_en   = field(&rec, DICT_T_DEF_EN, en, sizeof(en));
        ui_render_result(&t, &fnt, &e, extra);
    } else if (strcmp(mode, "typing") == 0) {
        dict_entry hits[8];
        ui_cand rows[8];
        static char words[8][64], trans[8][512];
        int n, i;

        n = dict_prefix(&d, (const uint8_t *)arg, (uint32_t)strlen(arg),
                        hits, 8, 128, 1);
        if (n < 0) {
            fprintf(stderr, "prefix 失敗 %d\n", n);
            return 3;
        }
        for (i = 0; i < n; i++) {
            dict_record rec;
            char *nl;
            uint32_t klen = 0;
            while (klen < DICT_KEY24 && hits[i].key24[klen])
                klen++;
            if (klen > sizeof(words[i]) - 1)
                klen = sizeof(words[i]) - 1;
            memcpy(words[i], hits[i].key24, klen);
            words[i][klen] = 0;
            rows[i].word = words[i];
            rows[i].trans = "";
            if (d.read_dat(d.dat_ctx, hits[i].off, hits[i].len, blob) == 0 &&
                dict_record_parse(blob, hits[i].len, &rec) == DICT_OK) {
                if (field(&rec, DICT_T_TRANS_ZH, trans[i], sizeof(trans[i]))) {
                    /* 候選清單只畫第一段 */
                    nl = strchr(trans[i], '\n');
                    if (nl)
                        *nl = 0;
                    rows[i].trans = trans[i];
                }
            }
        }
        ui_render_typing(&t, &fnt, arg, rows, n, extra);
    } else {
        fprintf(stderr, "不認得的模式 %s\n", mode);
        return 1;
    }

    if (write_ppm(out) != 0) {
        fprintf(stderr, "寫不了 %s\n", out);
        return 2;
    }
    printf("%s\t%s\tfont_reads=%u\tcolors=%u%s\n", mode, out,
           (unsigned)fnt.reads, (unsigned)FB.pal_n,
           FB.overflow ? "\tPALETTE-OVERFLOW" : "");
    return 0;
}
