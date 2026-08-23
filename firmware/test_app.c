/* 在 PC 上跑完整的前景：按鍵 -> 狀態機 -> 畫面，一樣不需要板子。
 *
 * 重點是**按鍵不是直接餵事件的**：腳本裡的每一顆鍵會先反查成矩陣座標，
 * 變成 74HC165 會讀回來的那 8 個 byte，再逐個 5ms 時間刻度餵給 keys.c。
 * 去彈跳、修飾鍵、連發因此都真的被跑過 —— 直接呼叫 app_key() 會讓這一段
 * 完全沒被驗證，那正是 HANDOVER「測試通過要先確認測試真的測到東西」講的
 * 那種假通過。
 *
 *   test_app <DICT目錄> keys                  鍵盤對照表自我檢查
 *   test_app <DICT目錄> run <腳本> <輸出前綴>  跑一段腳本，[SNAP] 處存圖
 *
 * 腳本裡可列印字元照打，特殊鍵寫成 [ENTER] [BS] [ESC] [UP] [DOWN]
 * [PGUP] [PGDN] [F1] [SNAP]。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "dict.h"
#include "fbuf.h"
#include "font.h"
#include "keys.h"
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

/* ---- 鍵盤對照表自我檢查 ----
 *
 * 這張表是從 README 的真值表機械轉出來的，轉錯了不會有任何症狀 ——
 * 直到有人按下那顆鍵。所以在這裡把它的結構性質全部量一次。 */
static int selftest_keys(void)
{
    int r, c, bad = 0, mods = 0, printable = 0;
    int seen[256];
    memset(seen, 0, sizeof(seen));

    for (r = 0; r < 8; r++) {
        for (c = 0; c < 8; c++) {
            uint8_t m = keys_mod(r, c);
            uint8_t b = keys_code(r, c, 0);
            uint8_t s = keys_code(r, c, 1);
            if (m) {
                mods++;
                if (b || s) {
                    printf("  FAIL  [%d][%d] 是修飾鍵卻也有鍵碼\n", r, c);
                    bad++;
                }
                continue;
            }
            if (!b || !s) {
                printf("  FAIL  [%d][%d] 沒有鍵碼（表沒填滿）\n", r, c);
                bad++;
                continue;
            }
            if (seen[b]) {
                printf("  FAIL  鍵碼 %d 出現在兩格\n", b);
                bad++;
            }
            seen[b] = 1;
            if (b >= 0x20 && b < 0x7F)
                printable++;
            /* 字母的 shift 版本必須是大寫，其餘不可與自己相同（除了
             * 空白與功能鍵）。 */
            if (keys_is_alpha(r, c) && !(b >= 'a' && b <= 'z' &&
                                         s == b - 32)) {
                printf("  FAIL  [%d][%d] 標成字母但 base/shift 不成對\n", r, c);
                bad++;
            }
        }
    }
    if (mods != 5) {
        printf("  FAIL  修飾鍵應該有 5 顆（SHIFT/CTRL/ALT/FN/CAPS），實際 %d\n",
               mods);
        bad++;
    }
    /* 64 格 = 5 顆修飾鍵 + 59 顆會送碼的鍵 */
    printf("  修飾鍵 %d、有鍵碼 %d、其中可列印 %d\n", mods, 64 - mods,
           printable);
    printf(bad ? "鍵盤對照表 %d 項不合格\n" : "鍵盤對照表通過\n", bad);
    return bad ? 1 : 0;
}

/* ---- 把一顆鍵反查回矩陣座標 ---- */

static int find_key(uint8_t code, int *row, int *col, int *shift)
{
    int r, c, s;
    for (s = 0; s < 2; s++)
        for (r = 0; r < 8; r++)
            for (c = 0; c < 8; c++)
                if (!keys_mod(r, c) && keys_code(r, c, s) == code) {
                    *row = r;
                    *col = c;
                    *shift = s;
                    return 1;
                }
    return 0;
}

static int find_mod(uint8_t mod, int *row, int *col)
{
    int r, c;
    for (r = 0; r < 8; r++)
        for (c = 0; c < 8; c++)
            if (keys_mod(r, c) == mod) {
                *row = r;
                *col = c;
                return 1;
            }
    return 0;
}

/* ---- 執行腳本 ---- */

static fbuf FB;
static app APP;
static keys KEYS;
static uint32_t NOW;
static int SNAPS;
static int SPOKE;

static void on_speak(void *ctx, const uint8_t *ids, int nbytes, int is_zh,
                     const char *fallback)
{
    (void)ctx;
    (void)is_zh;
    if (ids && nbytes >= 2)
        printf("  Fn+1 發音：%s（%d 個 id）\n", fallback ? fallback : "",
               nbytes / 2);
    else
        printf("  Fn+1 發音：%s（沒有音素資料，逐字母唸）\n",
               fallback ? fallback : "");
    SPOKE++;
}

/* 掃描一次並把事件送進狀態機。ms 是這一刻的時間。 */
static void tick(const uint8_t rows[8])
{
    key_event ev[KEYS_MAX_EVENTS];
    int n = keys_update(&KEYS, rows, NOW, ev, KEYS_MAX_EVENTS);
    int i;
    for (i = 0; i < n; i++)
        app_key(&APP, &ev[i]);
    NOW += 5;                    /* 板子上大約就是這個掃描週期 */
}

/* 按下再放開。兩邊都要撐過去彈跳時間，否則什麼都不會發生。 */
static void press(uint8_t code)
{
    uint8_t rows[8];
    int r, c, s, mr = 0, mc = 0, i, has_mod;
    uint8_t mod = 0;

    /* F1..F10 在這台機器上不是獨立按鍵，是 FN + 數字（生態系慣例）。
     * 腳本寫 [F1]，這裡就得真的按住 FN 再按 1 —— 否則 keys.c 的 FN
     * 轉換那一段永遠不會被跑到。 */
    if (code >= KEY_F1 && code <= KEY_F10) {
        mod = KEY_M_FN;
        code = (uint8_t)(code == KEY_F10 ? '0' : '1' + (code - KEY_F1));
    }

    if (!find_key(code, &r, &c, &s)) {
        fprintf(stderr, "腳本裡有鍵盤上沒有的鍵：%d\n", code);
        exit(3);
    }
    if (s)
        mod = KEY_M_SHIFT;
    has_mod = mod && find_mod(mod, &mr, &mc);

    memset(rows, 0, sizeof(rows));
    if (has_mod)
        rows[mr] |= (uint8_t)(1 << mc);
    /* 修飾鍵先按住幾個刻度，再按字元鍵 —— 真人就是這樣按的，而且這樣才
     * 會走到「修飾鍵先算完再決定字母大小寫」那條路。 */
    for (i = 0; i < 10; i++)
        tick(rows);
    rows[r] |= (uint8_t)(1 << c);
    for (i = 0; i < 10; i++)
        tick(rows);
    rows[r] &= (uint8_t)~(1 << c);
    for (i = 0; i < 10; i++)
        tick(rows);
    memset(rows, 0, sizeof(rows));
    for (i = 0; i < 10; i++)
        tick(rows);
}

static void snapshot(const char *prefix)
{
    char path[512];
    FILE *f;
    int x, y;

    app_render(&APP);
    snprintf(path, sizeof(path), "%s%02d.ppm", prefix, SNAPS++);
    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "寫不了 %s\n", path);
        exit(2);
    }
    fprintf(f, "P6\n%d %d\n255\n", UI_W, UI_H);
    for (y = 0; y < UI_H; y++)
        for (x = 0; x < UI_W; x++) {
            uint32_t col = fbuf_get(&FB, x, y);
            uint8_t rgb[3];
            rgb[0] = (uint8_t)(col >> 16);
            rgb[1] = (uint8_t)(col >> 8);
            rgb[2] = (uint8_t)col;
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    printf("  %s  %s  typed=%s  cand=%d sel=%d  scroll=%d/%d  %s\n", path,
           APP.state == APP_TYPING ? "TYPING" : "RESULT",
           APP.typed, APP.cand_n, APP.sel, APP.scroll, APP.body_lines,
           APP.state == APP_RESULT && APP.entry.headword
               ? APP.entry.headword : "");
}

static const struct { const char *name; uint8_t code; } TOKENS[] = {
    { "ENTER", KEY_ENTER }, { "BS", KEY_BS }, { "ESC", KEY_ESC },
    { "TAB", KEY_TAB }, { "DEL", KEY_DEL },
    { "UP", KEY_UP }, { "DOWN", KEY_DOWN }, { "LEFT", KEY_LEFT },
    { "RIGHT", KEY_RIGHT }, { "PGUP", KEY_PGUP }, { "PGDN", KEY_PGDN },
    { "F1", KEY_F1 }, { 0, 0 }
};

static void run_script(const char *script, const char *prefix)
{
    const char *p = script;
    while (*p) {
        if (*p == '[') {
            const char *end = strchr(p, ']');
            char name[16];
            size_t len;
            int i, hit = 0;
            if (!end) {
                fprintf(stderr, "腳本裡有沒關起來的 [\n");
                exit(3);
            }
            len = (size_t)(end - p - 1);
            if (len >= sizeof(name))
                len = sizeof(name) - 1;
            memcpy(name, p + 1, len);
            name[len] = 0;
            if (strcmp(name, "SNAP") == 0) {
                snapshot(prefix);
                hit = 1;
            }
            for (i = 0; !hit && TOKENS[i].name; i++)
                if (strcmp(name, TOKENS[i].name) == 0) {
                    press(TOKENS[i].code);
                    hit = 1;
                }
            if (!hit) {
                fprintf(stderr, "不認得的鍵 [%s]\n", name);
                exit(3);
            }
            p = end + 1;
        } else {
            press((uint8_t)*p++);
        }
    }
}

int main(int argc, char **argv)
{
    const char *dir, *mode;
    FILE *fidx, *fdat, *fcommon, *ffont;
    static file_ctx cidx, cdat, ccommon, cfont;
    static dict D;
    static font FNT;
    static ui_target T;
    int rc;

    if (argc < 3) {
        fprintf(stderr, "用法：test_app <DICT目錄> keys|run [腳本] [輸出前綴]\n");
        return 1;
    }
    dir = argv[1];
    mode = argv[2];

    if (strcmp(mode, "keys") == 0)
        return selftest_keys();

    if (argc < 5) {
        fprintf(stderr, "用法：test_app <DICT目錄> run <腳本> <輸出前綴>\n");
        return 1;
    }

    fidx = open_or_die(dir, "EC.IDX");
    fdat = open_or_die(dir, "EC.DAT");
    fcommon = open_or_die(dir, "ECC.IDX");
    ffont = open_or_die(dir, "FONT.BIN");
    cidx.f = fidx; cdat.f = fdat; ccommon.f = fcommon; cfont.f = ffont;

    memset(&D, 0, sizeof(D));
    if ((rc = dict_index_open(&D.main, file_read_sector, &cidx)) != DICT_OK) {
        fprintf(stderr, "EC.IDX 開檔失敗 %d\n", rc);
        return 2;
    }
    if (dict_index_open(&D.common, file_read_sector, &ccommon) == DICT_OK)
        D.has_common = 1;
    D.read_dat = file_read_at;
    D.dat_ctx = &cdat;

    if ((rc = font_open(&FNT, file_read_at, &cfont)) != FONT_OK) {
        fprintf(stderr, "FONT.BIN 開檔失敗 %d\n", rc);
        return 2;
    }

    {
        /* 板子上也是這樣掛的（RetroDict.ino）。不掛的話光是碼位表的二分搜尋
         * 就要每個字十幾次 SD 讀取 —— 這裡印出來的 font_reads 就是那個成本。 */
        static uint8_t cache[64 * 1024];
        uint32_t want = font_cache_size(&FNT, 1);
        uint32_t got = font_cache(&FNT, cache, sizeof(cache));
        fprintf(stderr, "字模快取：掛上 %u / 想要 %u bytes%s\n",
                (unsigned)got, (unsigned)want,
                got ? (FNT.has_ascii ? "（含 ASCII 字模）" : "（只有索引）")
                    : "（沒掛上）");
    }

    fbuf_init(&FB);
    fbuf_target(&FB, &T);
    keys_init(&KEYS);
    app_init(&APP, &D, &FNT, &T);
    APP.speak = on_speak;

    run_script(argv[3], argv[4]);
    printf("走完 %d 個時間刻度（%u ms），存了 %d 張圖，發音 %d 次，"
           "調色盤 %d 色%s\n", (int)(NOW / 5), (unsigned)NOW, SNAPS, SPOKE,
           FB.pal_n, FB.overflow ? "（超過 16 色！）" : "");
    return 0;
}
