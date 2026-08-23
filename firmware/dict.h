/* 字典查詢後台。FORMAT.md 的 C 實作。
 *
 * 這一層**不知道螢幕存在**（PLAN.md §4）。它只需要一個讀扇區的函式，
 * 所以在 PC 上餵檔案、在板子上餵 SD，程式碼完全相同。
 *
 * 沒有 malloc、沒有浮點數、沒有標準函式庫的檔案 I/O。
 * 最大的一塊 RAM 是每個索引一個 512 bytes 的扇區快取。
 */
#ifndef DICT_H
#define DICT_H

#include <stdint.h>
#include <stddef.h>

#define DICT_SECTOR        512
#define DICT_REC_SIZE      32
#define DICT_KEY24         24
#define DICT_RECS_PER_SEC  (DICT_SECTOR / DICT_REC_SIZE)   /* 16 */
#define DICT_MAX_SCAN      64      /* FORMAT.md §3.3 同鍵鄰居掃描上限 */
#define DICT_MAX_KEY       255

/* .DAT 欄位 tag（FORMAT.md §4.1） */
#define DICT_T_HEADWORD 0x01
#define DICT_T_PHONETIC 0x02
#define DICT_T_PINYIN   0x03
#define DICT_T_TRANS_ZH 0x04
#define DICT_T_DEF_EN   0x05
#define DICT_T_POS      0x06
#define DICT_T_EXCHANGE 0x07
#define DICT_T_FREQ     0x08
#define DICT_T_SYL_EN   0x09
#define DICT_T_SYL_ZH   0x0A
#define DICT_T_TRAD     0x0B

typedef enum {
    DICT_OK = 0,
    DICT_E_IO = -1,
    DICT_E_MAGIC = -2,
    DICT_E_VERSION = -3,
    DICT_E_RECSIZE = -4,
    DICT_E_MISMATCH = -5,
    DICT_E_NOTFOUND = -6,
    DICT_E_TOOBIG = -7
} dict_err;

/* 讀一個 512B 扇區。回 0 成功。呼叫端提供，這一層不碰 SD 也不碰檔案。 */
typedef int (*dict_read_fn)(void *ctx, uint32_t sector, uint8_t *out);

typedef struct {
    dict_read_fn read;
    void *ctx;
    uint32_t cached;          /* 目前快取的扇區號，DICT_NO_SECTOR = 無 */
    uint8_t buf[DICT_SECTOR];
    uint32_t reads;           /* 實際發生的讀取次數，供效能驗證 */
} dict_source;

#define DICT_NO_SECTOR 0xFFFFFFFFu

typedef struct {
    dict_source src;
    uint32_t rec_count;
    uint32_t dat_size;
    uint8_t  encoding;
    uint8_t  direction;
    uint16_t flags;
} dict_index;

typedef struct {
    dict_index main;
    dict_index common;        /* FORMAT.md §8，rec_count == 0 表示沒有 */
    int has_common;
    /* .DAT 的隨機存取。與扇區介面分開，因為 .DAT 讀的是任意 offset/len。 */
    int (*read_dat)(void *ctx, uint32_t off, uint32_t len, uint8_t *out);
    void *dat_ctx;
} dict;

typedef struct {
    uint8_t  key24[DICT_KEY24];
    uint32_t off;
    uint16_t len;
    uint16_t rank;
} dict_entry;

typedef struct {
    const uint8_t *key;
    uint8_t key_len;
    uint16_t rank;
    const uint8_t *blob;      /* 指向呼叫端提供的緩衝區 */
    uint16_t blob_len;
} dict_record;

/* --- 索引 --- */
int  dict_index_open(dict_index *ix, dict_read_fn read, void *ctx);
/* 第一筆 key24 >= key 的全域序號。FORMAT.md §3.2 */
uint32_t dict_lower_bound(dict_index *ix, const uint8_t *key, uint32_t key_len);
int  dict_index_read(dict_index *ix, uint32_t i, dict_entry *out);

/* --- 查詢 --- */
/* 同一個鍵可能對應**多筆**詞條：中文多音字（中 zhong1 / zhong4）、
 * 英文同形異義詞。UI 要能逐筆翻，所以用游標而不是一次回傳整個陣列 ——
 * 韌體端沒有 malloc，一次回傳陣列就得預先決定上限並占住 RAM。 */
typedef struct {
    uint32_t next;              /* 下一個要看的索引序號 */
    uint32_t scanned;           /* 已掃過幾筆，用來擋 DICT_MAX_SCAN */
    uint8_t  probe[DICT_KEY24];
    const uint8_t *key;
    uint32_t key_len;
} dict_cursor;

/* 回 1 表示 rec 已填、0 表示沒有（更多）結果、負值為錯誤。
 * buf 由呼叫端提供，需容得下一筆 .DAT 記錄；rec 內的指標指向 buf。 */
int dict_lookup_first(dict *d, const uint8_t *key, uint32_t key_len,
                      dict_cursor *cur, uint8_t *buf, uint32_t buf_size,
                      dict_record *rec);
int dict_lookup_next(dict *d, dict_cursor *cur,
                     uint8_t *buf, uint32_t buf_size, dict_record *rec);

/* 只要第一筆時的便利包裝。 */
int dict_lookup(dict *d, const uint8_t *key, uint32_t key_len,
                uint8_t *buf, uint32_t buf_size, dict_record *rec);

/* 前綴候選。common_first 是 FORMAT.md §8 的 A/B 開關。
 * 回傳填入 out 的筆數。 */
int dict_prefix(dict *d, const uint8_t *key, uint32_t key_len,
                dict_entry *out, int limit, int window, int common_first);

/* --- 記錄解碼 --- */
int dict_record_parse(const uint8_t *blob, uint32_t len, dict_record *rec);
/* 取出某個 tag 的欄位。找不到回 NULL。未知 tag 一律跳過不報錯。 */
const uint8_t *dict_field(const dict_record *rec, uint8_t tag, uint16_t *len);

/* --- 鍵的正規化（必須與 tools/dictbuild/normalize.py 完全一致） --- */
uint32_t dict_normalize_ec(const char *in, uint8_t *out, uint32_t out_size);

#endif /* DICT_H */
