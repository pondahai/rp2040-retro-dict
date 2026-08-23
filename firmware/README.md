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

`-Os`、Cortex-M0+、`-Wall -Wextra -Werror` 通過：

| | |
|---|---|
| 程式碼 | **1,444 bytes** |
| 靜態 RAM（data + bss） | **0** |
| `dict` 結構 | 1,092 bytes（兩個 512B 扇區快取佔掉大半） |
| `dict_cursor` | 40 bytes |
| `dict_entry` | 32 bytes |

沒有 malloc、沒有浮點數、沒有標準函式庫的檔案 I/O。
`.DAT` 的記錄緩衝區由呼叫端提供，大小自己決定。

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
| `dict.h` / `dict.c` | 查詢後台。要移植到板子上就是這兩個檔 |
| `test_compare.c` | PC 測試程式，把查詢結果印成 TSV |
| `compare.py` | 跟 Python 參考實作逐筆比對 |
| `build_pc.bat` | 用 Visual Studio 2022 編譯測試程式 |

---

## 還沒做

- **SD 卡驅動**（`dict_read_fn` 的板子端實作）
- **UI**：查詢框、結果頁、翻頁 —— 卡在 D3（字型 11×11 vs 16×16）
- **注音 IME**：要搬 `pico_keyboard_ime_terminal`，該 repo 本機還沒 clone
- **合成器的 C 版**：目前只有 Python（`tools/synth/`），演算法已驗證，
  移植是機械工作
- **`dict_normalize_ec` 只有英文方向**。中文的正規化目前只是去頭尾空白，
  韌體端要等 IME 接上時一起處理
