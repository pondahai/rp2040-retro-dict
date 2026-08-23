# firmware/ —— 查詢後台（C）

字典查詢的 C 實作。**目前只有後台，還沒有任何 UI 或硬體驅動。**

規格是 [../docs/FORMAT.md](../docs/FORMAT.md)，參考實作是
[../tools/dictbuild/container.py](../tools/dictbuild/container.py)。
這裡的 C 要與那份 Python **逐筆一致**，由 `compare.py` 驗證。

---

## 為什麼可以在 PC 上測

後台只依賴一個函式指標：

```c
typedef int (*dict_read_fn)(void *ctx, uint32_t sector, uint8_t *out);
```

在板子上餵 SD、在 PC 上餵檔案，`dict.c` 一行都不用改。這就是
PLAN.md §4「後台不知道螢幕存在、可在 PC 上測」的兌現 ——
**到目前為止整個專案沒有碰過硬體。**

---

## 量到的數字

`-Os`、Cortex-M0+：

| | 程式碼 | 靜態 RAM |
|---|---|---|
| `dict.c`（查詢後台） | **1,444 bytes** | 0 |
| `synth.c`（合成器） | **5,088 bytes** | 0 |
| `synth_tables.h`（參數表） | 1,938 bytes | — |
| **合計** | **約 8.5 KB** | 0 |

執行期的結構：`dict` 1,092 bytes（兩個 512B 扇區快取佔掉大半）、
`dict_cursor` 40 bytes、`dict_entry` 32 bytes。合成器另外需要一個 int32
暫存區（一個音節約 4,800 點 = 19 KB）。

沒有 malloc。查詢後台完全沒有浮點；合成器的係數計算用 double（每 32 個
取樣點才算一次），取樣迴圈本身是定點。

---

## 驗證

```
firmware\build_pc.bat
python firmware/compare.py 3000
```

拿**同一批** `out/DICT` 檔案，隨機抽樣字典裡真正存在的鍵，比對：

- 命中筆數（同一個鍵可能有多筆，見下）
- 每一筆的 rank 與詞頭
- **SD 讀取次數** —— 這條最嚴格，效能退化會立刻現形

已跑過 EC 3000 筆 + CE 750 筆 + 16 組前綴（A/B 兩種模式），全部一致。

### 兩個只有比對才會發現的問題

**① 同鍵多筆詞條。** 第一版的 `dict_lookup` 只回傳第一筆，而 Python 會收集
全部。中文多音字（`中` zhong1 / zhong4）、英文同形異義詞都是同一個鍵對應
多筆 —— 只回第一筆會**漏掉真實內容**。改成游標式（`dict_lookup_first` /
`dict_lookup_next`），UI 可以逐筆翻，也不需要預先配置陣列。

這個 bug 是靠「讀取次數差 1」發現的：Python 多讀那一次，正是為了確認
後面沒有更多同鍵詞條。

**② 漢英方向一筆都沒測到。** 比對腳本原本用命令列傳鍵，中文是 UTF-8
會被字碼頁弄壞，所以跳過非 ASCII 的鍵 —— 結果「比對通過」但半個字典
從來沒驗證過。改成十六進位傳鍵。

---

## 檔案

| | |
|---|---|
| `dict.h` / `dict.c` | 查詢後台 |
| `synth.h` / `synth.c` | 共振峰合成器（中英共用） |
| `synth_tables.h` | 參數表，**由 `tools/gen_tables.py` 產生，不要手改** |
| `test_compare.c` / `compare.py` | 查詢後台與 Python 逐筆比對 |
| `test_synth.c` / `compare_synth.py` | 合成器與 Python 比對 |
| `build_pc.bat` / `build_synth.bat` | 用 Visual Studio 2022 編譯 |

移植到板子上要的是 `dict.*`、`synth.*`、`synth_tables.h` 五個檔。

---

## 還沒做

- **SD 卡驅動**（`dict_read_fn` 的板子端實作）
- **UI**：查詢框、結果頁、翻頁。字型已定（16x16 2bit，見 tools/mkfont.py），
  排版已有預覽（tools/ui_preview.py），但韌體端還沒寫
- **注音 IME**：要搬 `pico_keyboard_ime_terminal`，該 repo 本機還沒 clone
- **`dict_normalize_ec` 只有英文方向**。中文的正規化目前只是去頭尾空白，
  韌體端要等 IME 接上時一起處理


---

## 合成器的比對

```
firmware/build_synth.bat
python firmware/compare_synth.py
```

定點運算不會跟 Python 位元相同，所以比的是**聽得出來的東西**：

- **長度**必須完全一致（時長與韻律規則有沒有搬對）
- **音高**在音節的 1/4 與 3/4 處各量一次，誤差 <4%
- **頻譜質心**誤差 <8%
- **對齊後的波形相關** >0.6（輔助指標，見下）

實測音高逐點吻合到 1 Hz 以內 —— 連三聲的低降升（90 → 262 Hz）都對得上。

### 為什麼波形相關只是輔助

兩個原因，都不是 bug：

1. **擦音起頭的音節根本不會相關**：兩邊亂數不同。那些改看**母音段**的
   頻譜，實測質心差 3–26 Hz。
2. **相鄰共振峰會互相拍頻**：`er` 的 F2=1350、F3=1500 很近，對係數的
   微小差異特別敏感，相關掉到 0.635 但質心差 0 Hz、音高完全一致。

### 移植時踩到的五個坑

每一個都會讓聲音壞掉，而且都是靠比對而不是靠耳朵發現的：

| 症狀 | 原因 |
|---|---|
| 頻譜質心從 858 掉到 133 Hz | 聲門低通的 `a` 只有 0.000378，Q12 量化後誤差 29%。改 Q20，那一級再改成不做 DC 正規化 |
| 波形被削爛 | 正規化**之前**就把中間值夾到 int16。共振器輸出本來就可能遠大於 int16，響度是最後才決定的 |
| 相關卡在 0.7 | 漏了整段音量包絡（12ms 淡入、30ms 淡出） |
| 上升調（二聲）相關掉到 0.5，平調卻沒事 | 相位步進 Q16 的捨入誤差**每個取樣點都往同方向累積**。改 Q24 |
| 聲調位置算錯 | 聲母段用了 `i/pre_len/4` 去近似，應該是 `i/total`。數值剛好接近，所以不會立刻看出來 |

---

## UI 排版與字模

```
firmware/build_ui.bat
python firmware/compare_ui.py
```

`font.c` 讀 `FONT.BIN`（`tools/mkfont.py` 的格式），`ui.c` 排版，兩者都
**不知道 ILI9341 存在**：字模走跟 `dict.c` 同款的讀取函式指標，像素往呼叫端
給的 `ui_target` 送。所以 PC 上畫成 PPM，板子上換成 DMA 送螢幕，中間那層
不用改。`-Os` Cortex-M0+ 下 `font.o` 997 B + `ui.o` 1,348 B，**靜態 RAM 0**
（唯一的大塊是呼叫端配置的 `font` 結構裡那個 512 B 區塊快取）。

比對的規格是 `tools/ui_preview.py`，而且比的是**每一個像素**，不是「看起來
像」：10 張畫面（5 個詞條 + 5 個前綴）320x240 全部逐點相同。不一致時會在
`out/font/` 寫出差異圖，Python 多出來的畫綠色、C 多出來的畫洋紅 —— 一眼
看得出是整行歪掉還是單一字模偏格。

挑的詞不是「查得到就好」，每個各踩一種排版邊界：`a`（釋義長到撞底部截斷）、
`run`（多段落 `
`）、`information`（窄字前進寬度差一格就整行歪）、
`resume`（音標含非 ASCII，走窄表的非 ASCII 分支）。

### 這裡踩到的坑

| 症狀 | 原因 |
|---|---|
| `UI_H` 巨集重複定義 | 標頭的 include guard 也叫 `UI_H`，跟畫面高度 240 撞名。guard 改成 `UI_H_INCLUDED` |
| 斷行寬度會偶爾算錯 | 段落複製進行緩衝區時按 byte 截斷，會把一個 UTF-8 序列切一半。截斷點要退回字元邊界（Python 是逐字元處理，沒有這個坑） |

窄表要**先於**寬表查（`font_get`），順序反了會讓全形／半形重疊的碼位拿到錯
的那個 —— 這跟 `mkfont.py` 的 `Font.glyph()` 是同一個順序。

---

## 鍵盤與狀態機

```
firmware/build_app.bat
firmware/test_app.exe ../out/DICT keys
python tools/ui_session.py "app[SNAP][DOWN][SNAP][ENTER][SNAP]"
```

`keys.c` 把 8x8 矩陣的掃描結果變成按鍵事件（去彈跳、修飾鍵、連發、
FN+數字 = F1~F10），`app.c` 是兩個畫面的狀態機（邊打邊查／詞條內文），
兩者都是純 C、都不碰 GPIO。板子上的 `loop()` 只有三行：掃矩陣、餵事件、
髒了就重畫。

**測試不是直接餵事件的。** `test_app.c` 把腳本裡的每一顆鍵反查成矩陣座標，
組成 74HC165 會讀回來的那 8 個 byte，再逐個 5ms 時間刻度餵進 `keys.c`。
直接呼叫 `app_key()` 會讓去彈跳與修飾鍵那一段完全沒被跑到 —— 那正是
HANDOVER 講的那種假通過。`[F1]` 也是真的按住 FN 再按 1。

對照表用 **KeyboardTester README 的實測真值表**，不是 `PicoApple2.ino` 的
`keymap_base`。`test_app.exe ... keys` 會檢查 64 格一對一填滿、修飾鍵恰好
5 顆、字母的 base/shift 成對 —— 表是機械轉出來的，轉錯了不會有任何症狀，
直到有人按下那顆鍵。

## 4bpp 畫布

`fbuf.c`。RGB565 全畫面要 150 KB，RP2040 只有 264 KB；但整個 UI 只用到
12 種顏色，存成 4 bit 索引只要 **37.5 KB**，送螢幕時逐列展開成 RGB565。
省下的 112 KB 比任何一種分區重繪都划算，而且 `ui.c` 只要畫一次。

`compare_ui.py` 的逐像素比對就是走這條路，所以打包／展開的正確性跟排版
一起被驗證。調色盤滿了會設 `overflow` 旗標而不是靜靜畫錯。

## 板子端（RetroDict/）

`RetroDict.ino` 是唯一碰硬體的檔案：ILI9341 初始化序列與鍵盤掃描時序
都自 PicoApple2 / KeyboardTester 原封搬移，SD 用 arduino-pico 的 `SD` 函式庫
接上 `dict_read_fn`。

`RetroDict/src/rd_*.c` 只是一行 `#include <dict.c>` —— **正本永遠是
`firmware/`**。Arduino 會把整個 sketch 目錄複製到 build 目錄再編譯，相對
路徑跳不出去，所以 `build_uf2.bat` 用 `-I` 把 `firmware/` 指進去，角括號
才不會 include 到自己。複製一份原始碼會立刻走樣，這個轉接檔就是為了不要
有第二份。

實測 flash 125,524 B（5%）、靜態 RAM 63,312 B（24%，含 37.5 KB 畫布與
SD 函式庫）。

---

## 效能：字模快取（實機上這一項決定「慢到不能用」還是「順」）

第一次上機的症狀是「打一個字母要等好幾秒」。原因不是 SD 卡慢，是**每畫一張
畫面要讀 SD 兩千多次**：

| | 每張畫面的 512B 讀取次數 |
|---|---|
| 沒有快取 | 邊打邊查 **2,346**、詞條 **1,694** |
| 掛上 `font_cache()` | 邊打邊查 **153**、詞條 **101** |

原因是 `font_get()` 要在 14,516 筆的碼位表上二分搜尋 14 步，而**每一步都落在
不同的 512B 區塊上** —— 一個字十幾次讀取。把兩張碼位表與窄字前進寬度搬進 RAM
（39,123 bytes，含 ASCII 字模）之後，只剩漢字點陣還要讀。

這個數字 `test_ui.exe` 從第一天就印在畫面上（`font_reads=2346`），但沒有人
（包括我）去看它 —— 它在 PC 上不會痛，因為 PC 讀檔案不用 1ms。**跨平台的
效能指標要在便宜的那一端就盯著看，不然它只會在貴的那一端現身。**

另一半是 SD 的 SPI 時脈：`SD.begin(cs, spi)` 走的是函式庫預設的
`SPI_HALF_SPEED` = **4MHz**，一個 512B 磁區光位元時間就 1ms。`RetroDict.ino`
改成由快到慢試 25 / 12 / 4 MHz，掛得起來就用那一階，並把結果印到序列埠。

快取本身不需要另外測 —— `compare_ui.py` 現在就是走快取那條路，14 張畫面
仍然逐像素相同。快取要是搬錯了一個 byte，那 14 張圖立刻會叫。
