# rp2040-retro-dict

RP2040 掌機上的英漢／漢英電子字典，具備 1980 年代電子字典風格的合成發音。
字典資料放 SD 卡，韌體本體常駐 flash。

[rp2040-retro-handheld](https://github.com/pondahai/rp2040-retro-handheld)
生態系的一員，設計上是 [rp2040-retro-loader](https://github.com/pondahai/rp2040-retro-loader)
選單裡的另一支 `.uf2` —— 因此**最終產物必須是偏移版（連結在 `0x10004000`）**，
理由見 [docs/PLAN.md](docs/PLAN.md) §2.8。

**目前狀態：PC 端完成；韌體可編譯成 uf2（螢幕、鍵盤、SD、狀態機都接好了），
但還沒燒進板子驗證過。**
接手請從 [HANDOVER.md](HANDOVER.md) 開始 —— 那裡有完整重建步驟與已定案事項。

## 它跟一般的做法哪裡不一樣

三個原本被列為最大風險的問題，最後是被同一個發現一起關掉的：

- **不需要 g2p 引擎**。字典本身就附發音資訊 —— ECDICT 每筆有音標、
  CC-CEDICT 每筆有帶調拼音。「從拼寫推發音」那件事，字典已經做完了。
- **不需要錄音**。1980 年代的電子字典是共振峰合成，不是拼接錄音 ——
  當年沒有記憶體放 1300 段錄音。那個「機器人腔」不是妥協，就是這個技術
  本身的聲音，而這正是本專案要的。
- **中英共用同一個合成器**。差別只在怎麼排共振峰軌跡，發聲機制完全相同。

結果是後台只有約 8.5 KB、靜態 RAM 0，取代了原本估計的 2MB 音節庫
加上一個英文合成引擎。

## 目錄

| | |
|---|---|
| `docs/` | 格式規格、實驗報告、原始規劃 |
| `tools/` | PC 端轉檔工具與合成器（純 Python，無第三方相依） |
| `firmware/` | 查詢後台、合成器、字模、排版、鍵盤、狀態機的 C 實作，全部可在 PC 上驗證 |
| `RetroDict/` | 板子上的 sketch —— 只有硬體膠水（ILI9341、鍵盤矩陣、SD） |
| `assets/` | 載入器選單的封面圖（96×96 RGB565）與它的來源圖 |

## 編譯

**要燒進板子的是偏移版**（連結在 `0x10004000`，前 16KB 留給載入器／跳板）：

```bash
build_offset.bat [arduino-cli 的路徑] [rp2040-retro-loader 的路徑]
```

產出兩個檔案：`build_offset/RetroDict.ino.uf2` 給已經有載入器的板子（丟 SD 卡
根目錄），`build_offset/RetroDict_standalone.uf2` 前面接上跳板，可以直接 USB 燒。
封面圖會依**兩個 uf2 各自的名字**複製兩份 —— 載入器是把 `.uf2` 砍掉再接
`.RAW`（`loader/thumb.c`），名字對不上就沒有封面。腳本也會跑
`loader_offset/check_flash_layout.py` —— 偏移錯了不會在編譯時報錯，
只會在實機上變成黑畫面，而且症狀跟「根本沒燒進去」一模一樣。選單封面圖
`assets/RetroDict.ino.RAW` 也會一起複製過去，兩個檔案都要放 SD 卡**根目錄**。

一般版（連結在 `0x10000000`，會蓋掉載入器，只適合單獨測試）：

```bash
build_uf2.bat [arduino-cli 的路徑]
```

產出 `build/RetroDict.ino.uf2`。板子端邏輯全部住在 `firmware/`，
`RetroDict/src/rd_*.c` 只是一行 include —— 所以**在 PC 上驗過的程式碼與板子上
跑的是同一份**。SD 卡要有 `/DICT/`（`EC.IDX`、`EC.DAT`、`ECC.IDX`、`FONT.BIN`），
產生方式見 HANDOVER。

## 授權

GPL-3.0。字典資料與字型各自遵守原授權：ECDICT（MIT）、
CC-CEDICT（CC-BY-SA）、Noto（SIL OFL 1.1）。
