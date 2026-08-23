# HANDOVER

給下一個接手的人（或下一段對話）。日期：2026-08-23。

**這份是入口。** 讀完再依需要進 `docs/`，不要從 `docs/PLAN.md` 開始 ——
那份有幾章已經被實作結果推翻，改寫過但仍以規劃視角書寫。

---

## 一句話

**已經在真機上跑起來了**：查英文、發音、注音打中文查漢英，都在板子上動過。

第一次上機只踩到兩個問題，而且都不在硬體那一層：SD 卡的路徑（`/DICT/`）
與**字模快取沒做**（每張畫面讀 SD 兩千多次，慢到不能用）。三段硬體膠水
（ILI9341 初始化、矩陣掃描、SD 開檔）一次就對 —— 那是 PLAN.md §4
「後台與前端分離」的直接結果。

---

## 現在有什麼

| | 狀態 |
|---|---|
| 字典檔格式 | 定稿，`docs/FORMAT.md` |
| PC 端轉檔工具 | 完成，`tools/`，47 項自我驗證 |
| 共振峰合成器（中英共用） | 完成，Python + C 兩份，比對通過 |
| 查詢後台 | C 實作完成，與 Python 逐筆比對通過 |
| 16×16 2bit 字模 | 完成，`tools/mkfont.py` |
| UI 排版 | C 實作完成，與 Python 預覽**逐像素**相同（含捲動） |
| 4bpp 畫布 | 完成，`firmware/fbuf.c`，37.5 KB |
| 鍵盤解碼 | 完成，`firmware/keys.c`，用實測真值表 |
| 前景狀態機 | 完成，`firmware/app.c`，兩個畫面 |
| 板子端 sketch | **已上機**：畫面、鍵盤、SD 都會動 |
| 偏移版編譯 | 完成，`build_offset.bat`，佈局檢查通過 |
| 選單封面圖 | 完成，`assets/RetroDict.ino.RAW`，`tools/mkicon.py` 可重產 |
| 發音 | 完成並實機驗過：Fn+1，有音標唸音標、沒有就用字母規則現場推 |
| CE 繁體化 | 完成：索引鍵與詞頭是繁體，簡體在 `SIMP` 欄 |
| 注音 IME | 完成並實機驗過：Fn+2 切換，1360 筆碼表逐筆比對 |

純 C 那幾支加起來約 **12.7 KB（不含表）、靜態 RAM 0**（`-Os`、Cortex-M0+）：`dict.c` 1,444 B、
`synth.c` 5,088 B + 參數表 1,938 B、`font.c` 997 B、`ui.c` 1,348 B、
`keys.c` 904 B、`app.c` 1,044 B、`fbuf.c` 460 B。

整支 sketch 編出來是 flash 125,956 B（6%）、靜態 RAM 104,288 B（39%）——
RAM 是 37.5 KB 的畫布 + 39 KB 的字模碼位表快取 + SD 函式庫。
那 39 KB **不是可有可無的最佳化**：沒有它，每畫一張畫面要讀 SD 兩千多次，
實機上按一個字母要等好幾秒（見 firmware/README「效能」）。

---

## 怎麼從零重建

repo 不含字典資料與字型（體積大、授權要各自遵守）。三步：

### 1. 下載原始資料到 `data/`

```bash
curl -L -o data/ecdict.csv https://raw.githubusercontent.com/skywind3000/ECDICT/master/ecdict.csv
curl -L -o data/cedict.txt.gz https://www.mdbg.net/chinese/export/cedict/cedict_1_0_ts_utf-8_mdbg.txt.gz
gunzip data/cedict.txt.gz
curl -L -o data/NotoSansSC-VF.otf https://github.com/notofonts/noto-cjk/raw/main/Sans/Variable/OTF/Subset/NotoSansSC-VF.otf
```

`ecdict.csv` 66MB（MIT）、`cedict.txt` 10MB（CC-BY-SA）、
`NotoSansSC-VF.otf` 15MB（OFL 1.1）。
字型另外用到 Windows 內建的 `NotoSansTC-VF.ttf` 與 `NotoSans-Regular.ttf`
（同為 OFL）—— 換平台要自己補這兩份，路徑寫在 `tools/mkfont.py` 的 `FONTS`。

### 2. 產生 SD 卡內容

```bash
python tools/mkdict.py ec data/ecdict.csv out/DICT
python tools/mkdict.py ce data/cedict.txt out/DICT
python tools/mkfont.py out/DICT
python tools/mkdict.py check out/DICT
```

產物約 113MB：`EC.IDX/DAT`、`CE.IDX/DAT`、`ECC.IDX`（常用詞索引）、
`FONT.BIN`。整個 `out/DICT` 複製到 SD 卡根目錄。

### 3. 驗證

```bash
python tools/tests/test_roundtrip.py
python firmware/compare.py 3000
python firmware/compare_synth.py
python tools/ui_preview.py
python tools/say.py --experiment
python firmware/compare_ui.py
firmware/test_app.exe out/DICT keys
python tools/ui_session.py
```

C 的部分要先用 `firmware/build_pc.bat`、`build_synth.bat`、`build_ui.bat` 與
`build_app.bat` 編譯（需要 Visual Studio 2022 Community）。

板子的 uf2（**要燒的是偏移版**）：

```bash
build_offset.bat [arduino-cli 的路徑] [rp2040-retro-loader 的路徑]
```

需要 arduino-cli 與 `rp2040:rp2040` 5.6.1。SD 卡要有 `/DICT/`
（`EC.IDX`、`EC.DAT`、`ECC.IDX`、`FONT.BIN`）。

實測佈局：image `0x10004000..0x10024200`（131,584 bytes），
向量表 `SP=0x20042000 Reset=0x100040e3`，載入器 `app_present()` 的條件都通過。
合併跳板後 `RetroDict_standalone.uf2` 578 blocks。封面圖
`assets/RetroDict.ino.RAW` 也會被複製到 `build_offset/`，與 uf2 同放 SD 根目錄。

---

## 已定案，不要再翻案

| 項目 | 決定 |
|---|---|
| 中文輸入 | **注音**，搬 `pico_keyboard_ime_terminal` 的引擎 |
| 最終產物 | **偏移版**（連結 `0x10004000`），見 PLAN.md §2.8 |
| 授權 | GPL-3.0 |
| 字典資料 | ECDICT + CC-CEDICT，不碰商業字典 |
| 繁簡 | **不做轉換**。來源有繁體就用繁體：CE 的索引鍵與詞頭是繁體、簡體降級成 `SIMP` 欄位；EC 的中文釋義只有簡體（ECDICT 沒有繁體），維持原樣 |
| 鍵盤對照表 | 用 KeyboardTester README 的**實測真值表** |
| 架構 | 後台與前端嚴格分離，後台可在 PC 上測 |
| 字典檔格式 | v1 定稿，見 FORMAT.md |
| 前綴候選 | 常用詞優先（模式 B）寫死，設定選單延後 |
| 發音 | **共振峰合成，中英共用一套**。不錄音、不用 SAM/eSpeak |
| 字型 | **16×16 2bit 灰階**，Noto 轉出的 `FONT.BIN` |

### 四個已經關閉的風險

- **U1**（Ekho 音檔授權）—— 不需要任何錄音，問題消失
- **U6**（英文合成器選型）—— 中英共用一套
- **U7**（flash 空間）—— 合成器參數表 < 2KB
- **U4**（16×16 字型來源）—— Noto，OFL 1.1

原本最大的三個未知數是被**同一個發現**一起關掉的：字典本身就附了發音資訊
（中文附拼音、英文附音標），所以不需要 g2p 引擎；而 1980 年代的電子字典
本來就是共振峰合成，不是拼接錄音 —— 當年沒有記憶體放 1300 段錄音。

---

## 還沒做的事（按建議順序）

1. **U3 的三組 A/B 聽判** —— 音檔在 `out/audio/`（`python tools/say.py
   --experiment` 產生）。比 `03` vs `04`（音節間隙）、`05` vs `06`
   （三聲變調）、`07` vs `08`（輕聲看不看前字）。
   **每個「聽不出差別」都代表一塊 `tools/synth/prosody.py` 的規則可以刪掉。**
   這是唯一還沒兌現的實驗價值，而且很便宜。
2. **D4**：生字本／考綱篩選要不要進第一版（ECDICT 已附 `tag` 欄位）
3. **整詞韻律** —— C 是逐音素接起來，Python 參考有跨音素平滑與句末降調
   （`english.py` 的 `plan()` + `_smooth()`）。`compare_synth.py` 只比過單一
   音素，整詞那段從來沒被驗過，聽起來就是「逐字拼」對「一句話」的差別
4. **發音長度上限** —— 現在 1.5 秒（`SPEAK_MAX_PCM` 24,000 取樣點），
   片語會被截掉（`kuroshio current` 1.42 秒已經貼著上限）。放寬到 2.5 秒
   約多 16KB RAM（66% -> 73%）
5. **簡體也能查**（可選）—— CE 現在只認繁體鍵。做法是索引加別名指向同一個
   `.DAT` 位置（只多 32 bytes/筆），要動 `container.build()`

---

## 這個專案最貴的教訓

寫在這裡是因為它反覆出現，而且每次都花掉最多時間。

### 一、「測試通過」要先確認測試真的測到東西

| 案例 | 症狀 |
|---|---|
| C/Python 比對 | 中文鍵是 UTF-8，走命令列會被字碼頁弄壞所以被跳過 —— 腳本印「完全一致」，但**半個字典從沒驗證過** |
| U3 聽判第一輪 | 測試檔 `01` 全是 `ma`，看似乾淨的對照組，讓「擦音比母音大 1700 倍」整類 bug 隱形 |
| 英文母音 | 測試檔把 `father` 與 `but` 寫進同一個字串，黏成一句念出來 |

### 二、壞的常常是尺，不是被量的東西

| 案例 | 假象 |
|---|---|
| 自相關沒正規化 | 把下降的四聲量成「上升到 276Hz」 |
| 粗糙 DFT 每 4 點取樣 | 3kHz 以上全是混疊，量出 `a` 和 `i` 的頻譜一模一樣 |
| 波形相關係數 | 擦音兩邊亂數不同本來就不會相關，`er` 的 F2/F3 靠太近會拍頻 —— 25 項全紅其實是指標選錯 |

現在 `tools/synth/spectrum.py` 用真正的 radix-2 FFT，不抽樣不取巧。

### 三、真實資料一定比規格髒

ECDICT 與 CC-CEDICT 挖出來的，全部寫成回歸測試：

- **同一個音有多種字元**：schwa 三種（西里爾 U+04D9 比真 IPA 還常見）、
  `ɛ` 三種（IPA／西里爾／希臘）、主重音三種
- **mojibake**：`^` 其實是 `g`、連續反斜線其實是 schwa
- **看起來像分隔符的其實不是**：逗號是**次重音記號**，拿去 split 會靜默
  丟掉 15% 的發音
- **`r5` 不是音節**，是兒化韻
- 字面的 `\n` 兩個字元，不還原就會印在螢幕上

同一類問題也出現在字型：`ASCII_FIRST` 定義是 `0x20`，正則只認十進位就會
抓到 `0`，整張 ASCII 表偏移 32 格（查 `A` 拿到 `a`）。

### 四、使用者的口語描述比自動檢查值錢

「只有 01 有出來」直接指出是擦音問題（01 全是 `ma`）。「`zi` `zhi` 只有
啾啾聲，其餘正常」直接把範圍縮到噪音路徑。這兩句省下的排查時間，
比所有自動檢查加起來還多。

---

## 判斷時沒讀到的資料

| repo | 為什麼需要 |
|---|---|
| `pico_keyboard_ime_terminal` | 注音 IME 引擎的正本。**已 clone**，碼表由 `tools/gen_ime_tables.py` 解析取用。上游沒有 LICENSE 檔、碼表出處未寫明 —— 散布前要補 |
| `rp2040-retro-loader/HANDOVER.md` | 已 clone 但沒讀過。整合前應該讀 |
| `pico_keyboard` | 鍵盤方案的前身，可能有佈線細節 |

---

## 順手改了別的 repo

`rp2040-ili9341-infones` 的 README：把 Cubic 11 的授權從含糊的
「著作權屬於原作者」改成明確的 **SIL OFL 1.1**（上游 `ACh-K/Cubic-11`，
衍生自 M⁺ gothic 12r），並補上「`font_cjk.h` 是衍生資料，散布時應附上
OFL.txt」。**已 commit，未 push。**

---

## 文件地圖

| | |
|---|---|
| `docs/FORMAT.md` | 字典檔格式。§3.2 的扇區二分搜尋有個容易寫錯的邊界；§8 是常用詞索引 |
| `docs/U3-REPORT.md` | 合成器實驗。§4.5–4.7 記錄三輪聽判各抓到的 bug |
| `docs/PLAN.md` | 原始規劃。§5 已依實作結果改寫，§6 的風險表多數已關閉 |
| `tools/README.md` | 轉檔工具與合成器（Python） |
| `firmware/README.md` | C 實作（後台／合成器／UI／鍵盤／狀態機）、比對方法、移植踩過的坑 |
| `RetroDict/RetroDict.ino` | 板子上唯一碰硬體的檔案：ILI9341、矩陣掃描、SD |
| `loader_offset/*.py` | 偏移編譯的 linker script 生成器與 build 期佈局檢查（自 KeyboardTester 搬來，**不要手改產出的 .ld**）|
