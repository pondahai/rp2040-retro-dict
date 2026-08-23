# tools/ —— PC 端轉檔工具

把 ECDICT 與 CC-CEDICT 的原始檔轉成 SD 卡上的 `/DICT/` 檔案。
格式規格在 [../docs/FORMAT.md](../docs/FORMAT.md)，這裡只講怎麼用。

需求：Python 3.8+，無第三方套件。

---

## 先取得原始資料

兩份都要自己下載，**repo 不含也不會含這些檔**（體積大，且 CC-CEDICT 是
CC-BY-SA，散布要附授權聲明）。

| 資料 | 檔名 | 授權 |
|---|---|---|
| ECDICT（英漢，約 76 萬筆） | `ecdict.csv` | MIT |
| CC-CEDICT（漢英，約 12 萬筆） | `cedict_ts.u8` | CC-BY-SA |

---

## 用法

```
python tools/mkdict.py ec  ecdict.csv    out/DICT
python tools/mkdict.py ce  cedict_ts.u8  out/DICT
python tools/mkdict.py check out/DICT
```

轉完把 `out/DICT` 整個目錄複製到 SD 卡根目錄。

`check` 會開啟產出的字典、實際跑一次查詢並**回報 SD 讀取次數** ——
用來確認效能沒有因為資料變動而退化，不是靠估算。

`python tools/mkdict.py syllables` 印出音節表，是之後自錄 1300 音節
（PLAN.md U1）時的錄音清單來源。

---

## 模組

| 檔案 | 職責 |
|---|---|
| `dictbuild/normalize.py` | 鍵的正規化。**PC 端與韌體端必須完全一致**，不一致會讓查詢靜默失敗 |
| `dictbuild/container.py` | `.IDX`/`.DAT` 讀寫。reader 部分是**查詢後台的參考實作**，韌體那份 C 要照它抄 |
| `dictbuild/syllable.py` | 音節表。由聲母 × 韻母規則生成，不是硬寫的清單 |
| `dictbuild/pinyin.py` | 拼音字串 → 音節 id，含變調 |
| `dictbuild/build_ec.py` | ECDICT CSV → EC.IDX/EC.DAT |
| `dictbuild/build_ce.py` | CC-CEDICT → CE.IDX/CE.DAT |
| `tests/test_roundtrip.py` | 自我驗證，**不需要真實字典資料** |

---

## 測試

```
python tools/tests/test_roundtrip.py
```

用合成資料把「轉檔 → 寫檔 → 二分搜尋 → 讀回」整條路走完，並實際量測
SD 讀取次數。這是 PLAN.md §4「後台可在 PC 上測」的第一個落實 ——
到目前為止還沒有一行程式碼需要板子。

---

## 已知限制

- **`SYL_EN`（英文發音，tag 0x09）尚未產生** —— 卡在 D2（SAM vs eSpeak-ng）
  未決。決定後在 `build_ec.py` 補一個 g2p 呼叫即可，索引與其他欄位都不必動。
- **音節表比標準拼音多約 17 個**。規則生成的副產物，是不存在的組合。
  它們只佔用不會被使用的 id，執行期無成本。真正該錄哪些音節，應由真實
  CC-CEDICT 轉檔的統計決定（已可跑），比手工修表可靠。
- **真實資料已跑過一輪**（見下表），但只抽驗了少數詞條。異體字、超長片語
  這類邊界情況仍可能有意外。
- **前綴候選是字母序，不是常用度序**。打 `hel` 得到 `helen/helena/held`，
  不是 `hello`。原因與兩個解法見 FORMAT.md §8，待決策。
- v1 不壓縮、不做繁簡轉換 —— 見 FORMAT.md §6。

---

## 真實資料實測（2026-08-23）

| | 詞條數 | 索引 | 內文 | 轉檔耗時 | 查詢 SD 讀取 |
|---|---|---|---|---|---|
| EC（ECDICT） | 770,611 | 24.7 MB | 71.4 MB | 11 s | 16 次 |
| CE（CC-CEDICT） | 124,889 | 4.0 MB | 12.1 MB | 1.7 s | 13 次 |

SD 佔用合計約 108 MB。讀取次數與合成資料的預測一致。

中文音節對應率 **99.98%**（約 30 萬個音節中 59 個對不上，是 cp/bp 之類的
縮寫、m/ng 語氣詞、與 biáng 這種特例）。另外分類統計出：兒化韻 692 次、
外來語字母名 139 次、資料自帶的無拼音標記 7 次 —— 這三類都已個別處理，
不會污染錄音清單。
