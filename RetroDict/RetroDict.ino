// ============================================================================
// RetroDict —— RP2040 掌機上的英漢字典
//
// 這支 sketch 只做三件事：
//
//   1. 把硬體叫起來（ILI9341、8x8 鍵盤矩陣、SD 卡）
//   2. 掃矩陣 -> keys.c -> app.c
//   3. app.c 說畫面髒了，就把 4bpp 畫布送上螢幕
//
// **邏輯一行都不在這裡。** 查詢（dict.c）、字模（font.c）、排版（ui.c）、
// 鍵盤解碼（keys.c）、狀態機（app.c）全部是純 C，在 PC 上跑過逐像素比對
// 與腳本測試（firmware/compare_ui.py、firmware/test_app.c）。這個檔案是
// 它們與這塊板子之間唯一的膠水，所以它壞掉的樣子只會有三種：畫面不亮、
// 按鍵不動、SD 讀不到。
//
// 接腳沿用生態系共用骨架（docs/PLAN.md §2.1），與 PicoApple2 / InfoNES /
// KeyboardTester 完全相同，同一塊板子可以燒不同韌體。
//
// 授權 GPL-3.0。TFT_DMA.cpp/.h 取自 PicoApple2（同為 GPL-3.0）。
// ============================================================================

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#include "TFT_DMA.h"
extern "C" {
#include "audio.h"
}

extern "C" {
#include "src/rd_app.h"
#include "src/rd_dict.h"
#include "src/rd_fbuf.h"
#include "src/rd_font.h"
#include "src/rd_keys.h"
#include "src/rd_ime.h"
#include "src/rd_lts.h"
#include "src/rd_speech.h"
#include "src/rd_ui.h"
}

// ---- 顯示器（spi0）----
#define PIN_DISPLAY_SCK  18
#define PIN_DISPLAY_MOSI 19
#define PIN_DISPLAY_CS   17
#define PIN_DISPLAY_DC   20
#define PIN_DISPLAY_RST  21
#define PIN_DISPLAY_BL   22

// ---- 鍵盤矩陣 ----
#define DATA_OUT_PIN     15
#define LATCH_PIN        14
#define CLOCK_PIN        26
#define DATA_IN_PIN      27

// ---- 喇叭 ----
// PicoApple2 的 PIN_JACK_SND 與 InfoNES 的 audio_init(7, ...) 都是這一支。
// PicoApple2 是 1-bit 直接翻轉 GPIO（Apple II 喇叭本來就那樣），共振峰合成
// 需要振幅，所以走 InfoNES 那條 PWM 的路。
#define PIN_SPEAKER      7

// ---- SD 卡（spi1）----
#define PIN_SD_CS        13
#define PIN_SD_SCK       10
#define PIN_SD_MOSI      11
#define PIN_SD_MISO      12

static TFT_DMA tft(PIN_DISPLAY_CS, PIN_DISPLAY_DC, PIN_DISPLAY_RST,
                   PIN_DISPLAY_MOSI, PIN_DISPLAY_SCK);

// ============================================================================
// SD：把 dict.c / font.c 要的讀取函式接到檔案上
//
// 後台要的是「從某個 offset 讀 n 個 byte」，SD 函式庫給的是檔案物件，
// 中間只差一個 seek。PC 上那份（test_compare.c / test_ui.c）長得一模一樣，
// 只是 FILE* 換成 File —— 這就是後台與硬體分離換來的東西。
// ============================================================================

struct FileCtx { File f; };

static FileCtx g_ec_idx, g_ec_dat, g_ecc_idx, g_font_file;
static FileCtx g_ce_idx, g_ce_dat;

static int sd_read_at(void *ctx, uint32_t off, uint32_t len, uint8_t *out)
{
    FileCtx *fc = (FileCtx *)ctx;
    // 讀 SD 是整支韌體最慢的動作，而音訊的雙緩衝要人定期去填。與其祈禱
    // 主迴圈夠快，不如**在慢的地方順手餵一次** —— 沒東西要填時它會立刻返回。
    audio_mixer_step();
    if (!fc->f)
        return -1;
    if (!fc->f.seek(off))
        return -1;
    int got = fc->f.read(out, len);
    if (got < 0)
        return -1;
    // 檔尾那個不滿的區塊要補零：字模檔的最後一次讀一定會踩到。
    if ((uint32_t)got < len)
        memset(out + got, 0, len - got);
    return 0;
}

static int sd_read_sector(void *ctx, uint32_t sector, uint8_t *out)
{
    return sd_read_at(ctx, sector * DICT_SECTOR, DICT_SECTOR, out);
}

// ============================================================================
// 鍵盤矩陣（時序自 PicoApple2.ino scan_matrix() 原封搬移）
//
// 唯一的改動是輸出格式：這裡直接組成 keys.c 要的「每列一個 byte」，
// bit c = [row][col] 被按住。原版是 keyState[row][7-col]，所以這裡也要
// 反過來取位元 —— 這個 7-col 是 74HC165 的接線順序，不是筆誤。
// ============================================================================

static inline void fastWrite(uint pin, bool val) { gpio_put(pin, val); }
static inline bool fastRead(uint pin) { return gpio_get(pin); }

static uint8_t myShiftIn(uint8_t dP, uint8_t cP)
{
    uint8_t data = 0;
    for (int i = 0; i < 8; i++) {
        if (fastRead(dP))
            data |= (1 << i);
        fastWrite(cP, 1);
        delayMicroseconds(1);
        fastWrite(cP, 0);
    }
    return data;
}

static void scanMatrix(uint8_t rows[8])
{
    for (int row = 0; row < 8; row++) {
        uint8_t rS = (uint8_t)(1 << row);
        fastWrite(LATCH_PIN, 0);
        shiftOut(DATA_OUT_PIN, CLOCK_PIN, MSBFIRST, 0);
        shiftOut(DATA_OUT_PIN, CLOCK_PIN, MSBFIRST, rS);
        fastWrite(LATCH_PIN, 1); delayMicroseconds(5);
        fastWrite(LATCH_PIN, 0); delayMicroseconds(1);
        fastWrite(LATCH_PIN, 1);

        uint8_t colData = myShiftIn(DATA_IN_PIN, CLOCK_PIN);
        uint8_t bits = 0;
        for (int col = 0; col < 8; col++)
            if (colData & (1 << (7 - col)))
                bits |= (uint8_t)(1 << col);
        rows[row] = bits;
    }
    fastWrite(CLOCK_PIN, 1); delayMicroseconds(2); fastWrite(CLOCK_PIN, 0);
}

// ============================================================================
// 畫布送上螢幕
//
// 畫布是 4bpp 調色盤（37.5 KB），這裡逐列展開成 RGB565 再送。兩個列緩衝
// 輪流用：一邊 DMA 在送，一邊 CPU 展開下一列，所以展開的成本幾乎是免費的。
// 320x240 一整張 = 150 KB over SPI，62.5 MHz 下約 20 ms —— 字典是按一下
// 更新一次，不是每秒 60 張，這個速度綽綽有餘。
// ============================================================================

static fbuf g_fb;
static uint8_t g_line[2][FB_W * 2];

// 這幾個的定義在下面（順序是為了讓宣告貼近它們所屬的段落），
// 但 on_speak() 與 refresh_status() 在那之前就要用到。
static app g_app;
static void refresh_status();

static dict g_dict;
static dict g_dict_ce;
static bool g_has_ce = false;
static font g_font_dev;

// 發音用的三塊緩衝。整段唸完的波形先收在 g_pcm，再一次交給 DMA 播 ——
// 一個字最長 1.5 秒，邊合成邊播反而要處理 underrun，不划算。
// 單一音節／音素的長度上限。**不要寫死**：原本是 4000（0.25 秒），照
// 英文音素的長度抓的，但中文三聲是 300ms、句末拉長後 345ms —— 每個三聲
// 都被 syn_syllable() 靜靜截掉一截，沒有任何錯誤訊息，是 compare_synth.py
// 的整串長度比對才抓到的。現在由 tools/gen_tables.py 從 prosody.py 的
// TONE_DURATION × FINAL_LENGTHEN 算出來。
#define SPEAK_MAX_SEG   SYN_MAX_SEG_SAMPLES
#define SPEAK_MAX_PCM   24000       // 整段上限 1.5 秒
static int32_t g_syn_work[SPEAK_MAX_SEG];
static int16_t g_syn_seg[SPEAK_MAX_SEG];
static uint8_t g_syn_pcm8[SPEAK_MAX_SEG];
static uint8_t g_pcm[SPEAK_MAX_PCM];
static int g_pcm_n;
static speech g_speech;

// speech.c 一段一段吐波形，這裡接起來。滿了就丟掉尾巴 —— 寧可少唸幾個
// 音素，也不要蓋掉 DMA 正在讀的那塊記憶體。
static void pcm_sink(void *ctx, const uint8_t *pcm, int n)
{
    (void)ctx;
    if (g_pcm_n + n > SPEAK_MAX_PCM)
        n = SPEAK_MAX_PCM - g_pcm_n;
    if (n <= 0)
        return;
    memcpy(g_pcm + g_pcm_n, pcm, (size_t)n);
    g_pcm_n += n;
}

// 純方波測試音。用途是**把病因切開**：按 Fn+2 聽得到嗶聲，就表示 PWM、DMA、
// 接線、喇叭都是好的，問題在合成那一側；還是只有卡答聲，那就是音訊路徑本身。
// 卡答聲的來源是 DMA 沒有波形可播，PWM 位準只跳了一下。
static int g_speak_src = -1;

static void speak_stop(void)
{
    if (g_speak_src >= 0) {
        audio_source_stop(g_speak_src);
        g_speak_src = -1;
    }
}

static void tone(int hz, int ms)
{
    int n = SYN_SR * ms / 1000;
    int half = SYN_SR / (hz * 2);
    if (n > SPEAK_MAX_PCM)
        n = SPEAK_MAX_PCM;
    if (half < 1)
        half = 1;
    for (int i = 0; i < n; i++) {
        // 兩端不要用 0/254，直流偏移太大會讓喇叭「砰」一聲
        g_pcm[i] = ((i / half) & 1) ? 180 : 74;
    }
    g_pcm_n = n;
    speak_stop();
    g_speak_src = audio_play_once(g_pcm, n);
}

static void on_speak(void *ctx, const uint8_t *ids, int nbytes, int is_zh,
                     const char *fallback)
{
    (void)ctx;
    speak_stop();               // 上一次還在播就打斷它
    g_pcm_n = 0;
    speech_init(&g_speech, pcm_sink, NULL, g_syn_work, g_syn_seg, g_syn_pcm8,
                SPEAK_MAX_SEG);
    if (ids && nbytes >= 2) {
        speech_ids(&g_speech, ids, nbytes, is_zh);      // 有音標就唸音標
    } else if (speech_letters(&g_speech, fallback) <= 0) {
        // 沒有音標 -> 用字母規則現場推（lts.c）。連規則都推不出東西
        // （純數字、純符號）才退回逐字母唸。
        speech_spell(&g_speech, fallback);
    }
    Serial.printf("speak: %d samples (ids=%d)\n", g_pcm_n, nbytes / 2);
    if (g_pcm_n <= 0) {
        // 合成一個取樣點都沒產出。低頻短嗶 = 「有收到 Fn+1，但沒東西可唸」，
        // 跟音訊路徑壞掉的卡答聲區分得開。
        tone(200, 120);
        return;
    }
    g_speak_src = audio_play_once(g_pcm, g_pcm_n);
    g_app.speaking = 1;
    refresh_status();
}
// 40KB：兩張碼位表 + 窄字前進寬度 + ASCII 字模，實測要 39,123 bytes。
static uint8_t g_font_cache[40 * 1024];
static ui_target g_target;
static keys g_keys;

// 只送某幾條掃描線。發音狀態變化時用它 —— 整頁重畫要從 SD 讀上百次字模、
// 一次一百多毫秒，音訊緩衝區會來不及填而斷音（那正是「aaaaple」的成因）。
static void blit_rows(int y0, int h)
{
    tft.startFrame(0, y0, UI_W - 1, y0 + h - 1);
    for (int y = y0; y < y0 + h; y++) {
        uint8_t *buf = g_line[y & 1];
        fbuf_line_rgb565(&g_fb, y, buf);
        tft.waitTransferDone();
        tft.sendScanlineAsync((const uint16_t *)buf, FB_W);
    }
    tft.waitTransferDone();
    digitalWrite(PIN_DISPLAY_CS, HIGH);
}

// 右下角的狀態格單獨重畫（模式、大小寫、發音中）。
static void refresh_status()
{
    ui_draw_status(&g_target, &g_font_dev, app_status(&g_app));
    blit_rows(UI_H - UI_LINE_H, UI_LINE_H);
}

static void blit()
{
    tft.startFrame(0, 0, UI_W - 1, UI_H - 1);
    for (int y = 0; y < UI_H; y++) {
        if ((y & 31) == 0)
            audio_mixer_step();      // 整張畫面約 20ms，中途也要餵
        uint8_t *buf = g_line[y & 1];
        fbuf_line_rgb565(&g_fb, y, buf);
        tft.waitTransferDone();
        tft.sendScanlineAsync((const uint16_t *)buf, FB_W);
    }
    tft.waitTransferDone();
    digitalWrite(PIN_DISPLAY_CS, HIGH);
}

// ============================================================================


// 開機或出錯時直接畫在螢幕上：這台機器沒有序列埠可看的時候仍要說得出話。
static void banner(const char *msg)
{
    ui_entry e;
    memset(&e, 0, sizeof(e));
    e.headword = "RetroDict";
    e.trans_zh = msg;
    ui_render_result(&g_target, &g_font_dev, &e, 0, "RetroDict", NULL);
    blit();
}

static void display_begin()
{
    // TFT_DMA::begin() 只設定 GPIO / SPI / DMA，不含面板本身的初始化序列。
    // 整段（含背光腳與硬體 reset 脈衝）自 PicoApple2.ino setup1() 原封搬移
    // —— 少了它畫面就是全黑。
    pinMode(PIN_DISPLAY_BL, OUTPUT); digitalWrite(PIN_DISPLAY_BL, LOW);

    tft.begin();
    gpio_put(PIN_DISPLAY_RST, 0); delay(100);
    gpio_put(PIN_DISPLAY_RST, 1); delay(100);
    tft.writeCommand(0x01); delay(150);
    tft.writeCommand(0xCB); tft.writeData(0x39); tft.writeData(0x2C);
                            tft.writeData(0x00); tft.writeData(0x34);
                            tft.writeData(0x02);
    tft.writeCommand(0xCF); tft.writeData(0x00); tft.writeData(0xC1);
                            tft.writeData(0x30);
    tft.writeCommand(0xE8); tft.writeData(0x85); tft.writeData(0x00);
                            tft.writeData(0x78);
    tft.writeCommand(0xEA); tft.writeData(0x00); tft.writeData(0x00);
    tft.writeCommand(0xED); tft.writeData(0x64); tft.writeData(0x03);
                            tft.writeData(0x12); tft.writeData(0x81);
    tft.writeCommand(0xF7); tft.writeData(0x20);
    tft.writeCommand(0xC0); tft.writeData(0x23);
    tft.writeCommand(0xC1); tft.writeData(0x10);
    tft.writeCommand(0xC5); tft.writeData(0x3E); tft.writeData(0x28);
    tft.writeCommand(0xC7); tft.writeData(0x86);
    tft.setRotation(1);                     // 橫的，320x240
    tft.writeCommand(0x3A); tft.writeData(0x55);
    tft.writeCommand(0xB1); tft.writeData(0x00); tft.writeData(0x18);
    tft.writeCommand(0xB6); tft.writeData(0x08); tft.writeData(0x82);
                            tft.writeData(0x27);
    tft.writeCommand(0x11); delay(150);      // sleep out
    tft.writeCommand(0x29); delay(150);      // display on

    spi_set_baudrate(spi0, 62500000);
    tft.fillScreen(0x0000);
    digitalWrite(PIN_DISPLAY_BL, HIGH);      // 初始化完才點背光，避免雪花
}

// SD 卡的 SPI 時脈。函式庫預設是 SPI_HALF_SPEED = 4MHz，光是打完一個 512B
// 磁區的位元就要 1ms —— 字典是整個生命週期都在讀 SD 的東西（PLAN.md 3），
// 這個預設值等於把每次查詢都乘上四倍。25MHz 是 SPI 模式的常見上限，接線品質
// 不好時會掛不起來，所以由快到慢試，掛得起來就用那一階。
static const uint32_t SD_SPEEDS[] = { SD_SCK_MHZ(25), SD_SCK_MHZ(12),
                                      SD_SCK_MHZ(4) };
static uint32_t g_sd_mhz = 0;

static bool sd_begin()
{
    SPI1.setRX(PIN_SD_MISO);
    SPI1.setTX(PIN_SD_MOSI);
    SPI1.setSCK(PIN_SD_SCK);

    for (size_t i = 0; i < sizeof(SD_SPEEDS) / sizeof(SD_SPEEDS[0]); i++) {
        if (SD.begin(PIN_SD_CS, SD_SPEEDS[i], SPI1)) {
            g_sd_mhz = SD_SPEEDS[i] / 1000000UL;
            Serial.printf("SD: %lu MHz\n", (unsigned long)g_sd_mhz);
            break;
        }
        SD.end();
    }
    if (!g_sd_mhz)
        return false;

    g_ec_idx.f  = SD.open("/DICT/EC.IDX",  FILE_READ);
    g_ec_dat.f  = SD.open("/DICT/EC.DAT",  FILE_READ);
    g_ecc_idx.f = SD.open("/DICT/ECC.IDX", FILE_READ);
    g_font_file.f = SD.open("/DICT/FONT.BIN", FILE_READ);
    // 漢英是選用的：沒有 CE.* 就只是 Fn+2 不作用，其餘照常。
    g_ce_idx.f = SD.open("/DICT/CE.IDX", FILE_READ);
    g_ce_dat.f = SD.open("/DICT/CE.DAT", FILE_READ);
    return g_ec_idx.f && g_ec_dat.f && g_font_file.f;
}

void setup()
{
    Serial.begin(115200);

    gpio_init(DATA_OUT_PIN); gpio_set_dir(DATA_OUT_PIN, GPIO_OUT);
    gpio_init(LATCH_PIN);    gpio_set_dir(LATCH_PIN, GPIO_OUT);
    gpio_init(CLOCK_PIN);    gpio_set_dir(CLOCK_PIN, GPIO_OUT);
    gpio_init(DATA_IN_PIN);  gpio_set_dir(DATA_IN_PIN, GPIO_IN);

    display_begin();
    fbuf_init(&g_fb);
    fbuf_target(&g_fb, &g_target);

    if (!sd_begin()) {
        // 字模也在 SD 上，所以這裡連字都畫不出來 —— 只能用底色說話。
        tft.fillScreen(0xF800);
        Serial.println("SD / DICT open failed");
        return;
    }

    if (font_open(&g_font_dev, sd_read_at, &g_font_file) != FONT_OK) {
        tft.fillScreen(0xF800);
        Serial.println("FONT.BIN open failed");
        return;
    }

    // 字模的碼位表搬進 RAM。**這一步不是可有可無的最佳化**：沒有它，每畫一個
    // 字要在 14,516 筆的碼位表上二分搜尋 14 步，每一步落在不同的 512B 區塊上
    // —— 一張畫面 2,346 次 SD 讀取，按一個字母要等好幾秒。掛上之後 153 次。
    // 剩下的空間拿去放 ASCII 字模，英文畫面幾乎整頁都是它。
    {
        uint32_t got = font_cache(&g_font_dev, g_font_cache,
                                  sizeof(g_font_cache));
        Serial.printf("font cache: %lu / %lu bytes%s\n",
                      (unsigned long)got,
                      (unsigned long)font_cache_size(&g_font_dev, 1),
                      g_font_dev.has_ascii ? " (with ASCII)" : "");
    }

    memset(&g_dict, 0, sizeof(g_dict));
    if (dict_index_open(&g_dict.main, sd_read_sector, &g_ec_idx) != DICT_OK) {
        banner("EC.IDX 讀不到或格式不對");
        return;
    }
    if (dict_index_open(&g_dict.common, sd_read_sector, &g_ecc_idx) == DICT_OK)
        g_dict.has_common = 1;
    g_dict.read_dat = sd_read_at;
    g_dict.dat_ctx = &g_ec_dat;

    audio_init(PIN_SPEAKER, SYN_SR);

    if (g_ce_idx.f && g_ce_dat.f) {
        memset(&g_dict_ce, 0, sizeof(g_dict_ce));
        if (dict_index_open(&g_dict_ce.main, sd_read_sector, &g_ce_idx)
            == DICT_OK) {
            g_dict_ce.read_dat = sd_read_at;
            g_dict_ce.dat_ctx = &g_ce_dat;
            g_has_ce = true;
        }
    }
    Serial.printf("dict: EC ok, CE %s\n", g_has_ce ? "ok" : "missing");

    keys_init(&g_keys);
    app_init(&g_app, &g_dict, g_has_ce ? &g_dict_ce : NULL, &g_font_dev,
             &g_target);
    g_app.speak = on_speak;
    app_render(&g_app);
    blit();
}

void loop()
{
    uint8_t rows[8];
    key_event ev[KEYS_MAX_EVENTS];

    scanMatrix(rows);
    int n = keys_update(&g_keys, rows, millis(), ev, KEYS_MAX_EVENTS);
    for (int i = 0; i < n; i++) {
        // Fn+9 = 音訊自我測試。留著是因為它當初把「合成沒產出」與「音訊路徑
        // 壞掉」兩種一模一樣的卡答聲分開了，下次接線動到還會需要。
        // （本來借的是 Fn+2，但那顆現在是英漢/漢英切換 —— 撞在一起就會變成
        // 「按切換卻發出嗶聲」。）
        if (ev[i].code == KEY_F9) {
            Serial.println("test tone 1kHz");
            tone(1000, 300);
            continue;
        }
        app_key(&g_app, &ev[i]);
    }

    // 緩衝區只有 1024 個樣本（16kHz 下 64ms），所以每一圈都要餵。查詞或
    // 重畫畫面會佔掉幾十毫秒，這也是為什麼發音時不要同時做那些事。
    audio_mixer_step();

    // CapsLock 是修飾鍵，**不會產生按鍵事件** —— 所以不能等 app_key() 來更新
    // 狀態格，否則要按下一個鍵右下角才會變。這裡直接看鍵盤層的狀態。
    if (g_app.caps != (int)g_keys.caps) {
        g_app.caps = g_keys.caps;
        refresh_status();
    }

    // 播完了就把「音」收起來。只重畫右下角那一格，不整頁重畫。
    if (g_app.speaking && g_speak_src >= 0 &&
        !audio_is_source_active(g_speak_src)) {
        g_app.speaking = 0;
        g_speak_src = -1;
        refresh_status();
    }

    if (app_render(&g_app))
        blit();

    delay(5);      // 掃描週期。去彈跳是 30ms，這裡不需要更快。
}
