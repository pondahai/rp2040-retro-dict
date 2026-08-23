# rp2040-retro-dict

RP2040 掌機上的英漢／漢英電子字典，具備早期電子字典風格的合成發音。
字典資料放 SD 卡，韌體本體常駐 flash。

[rp2040-retro-handheld](https://github.com/pondahai/rp2040-retro-handheld)
生態系的一員，設計上是 [rp2040-retro-loader](https://github.com/pondahai/rp2040-retro-loader)
選單裡的另一支 `.uf2` —— 因此**最終產物必須是偏移版（連結在 `0x10004000`）**，理由見 PLAN.md §2.8。

**目前狀態：只有計畫，沒有程式碼。** 規劃、已知條件與未解問題全在
[docs/PLAN.md](docs/PLAN.md)。動手前請先讀那份，
接手開發請先讀 [HANDOVER.md](HANDOVER.md)。

## 一句話的可行性結論

可行，而且比同生態系的 Doom / NES 移植輕鬆 —— 字典是「查完才畫、按了才發音」，
沒有即時運算壓力。真正的新工作只有四項：字典索引、英文合成、中文音節拼接、
（若走注音）IME 移植。其餘模組在既有專案裡都有成熟前例。
