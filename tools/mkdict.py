#!/usr/bin/env python3
"""把 ECDICT / CC-CEDICT 轉成 SD 卡上的 /DICT/ 檔案。

  python tools/mkdict.py ec  ecdict.csv     out/DICT
  python tools/mkdict.py ce  cedict_ts.u8   out/DICT
  python tools/mkdict.py check out/DICT

轉完把 out/DICT 整個目錄複製到 SD 卡根目錄即可。
"""

import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from dictbuild import container as C  # noqa: E402
from dictbuild import build_ce, build_ec, syllable  # noqa: E402
from dictbuild.normalize import normalize_ce, normalize_ec  # noqa: E402


def _report(stats):
    unknown = stats.pop("unknown_syllables", None)
    for k, v in stats.items():
        print("  %-20s %s" % (k, v))
    if unknown:
        top = sorted(unknown.items(), key=lambda kv: -kv[1])[:15]
        print("  無法對應的音節 %d 種（合計 %d 次），最常見："
              % (len(unknown), sum(unknown.values())))
        print("   ", ", ".join("%s x%d" % kv for kv in top))
        print("  ↑ 這些要嘛是音節表漏了，要嘛是資料裡的外來語。錄音前先看這份。")


def cmd_build(kind, src, outdir):
    os.makedirs(outdir, exist_ok=True)
    name = "EC" if kind == "ec" else "CE"
    idx = os.path.join(outdir, name + ".IDX")
    dat = os.path.join(outdir, name + ".DAT")
    # 常用詞索引（FORMAT.md §8）只對英漢有意義：ECDICT 附真實詞頻，
    # CC-CEDICT 沒有。中文那邊硬做只是把長度啟發式包裝成「常用度」，
    # 實測會把「你个头」排在「你好」前面 —— 寧可不做。
    common = os.path.join(outdir, name + "C.IDX") if kind == "ec" else None
    t0 = time.time()
    mod = build_ec if kind == "ec" else build_ce
    stats = mod.build(src, idx, dat, common_idx_path=common)
    extra = "，常用詞索引 %d bytes" % os.path.getsize(common) if common else ""
    print("%s: %d 筆，索引 %d bytes%s，內文 %d bytes，耗時 %.1fs"
          % (name, stats["entries"], os.path.getsize(idx), extra,
             stats["dat_bytes"], time.time() - t0))
    _report(stats)


def cmd_check(outdir):
    """開一次字典、量實際 SD 讀取次數。FORMAT.md §3.2 的估算要能被驗證。"""
    for name, norm, probe in (("EC", normalize_ec, "hello"),
                              ("CE", normalize_ce, "你好")):
        idx = os.path.join(outdir, name + ".IDX")
        dat = os.path.join(outdir, name + ".DAT")
        if not os.path.exists(idx):
            print("%s: 缺檔，跳過" % name)
            continue
        common = os.path.join(outdir, name + "C.IDX")
        d = C.Dictionary(idx, dat, common if os.path.exists(common) else None)
        print("%s: %d 筆（常用詞 %s）source=%s"
              % (name, d.hdr.rec_count,
                 d.common.hdr.rec_count if d.common else "無", d.hdr.source_tag))
        d.src.reads = 0
        hits = d.lookup(norm(probe))
        print("   查 %-6s 命中 %d 筆，SD 讀取 %d 次"
              % (probe, len(hits), d.src.reads))
        # FORMAT.md §8 的兩種模式並排，方便直接看差別
        pre = norm(probe)[:3]
        for label, cf in (("B 常用詞優先", True), ("A 純字母序", False)):
            got = d.prefix(pre, 6, common_first=cf)
            shown = [k.decode("utf-8", "replace") for k, _o, _l, _r in got]
            print("   前綴 %-8s %-14s %s"
                  % (pre.decode("utf-8", "replace"), label, " ".join(shown)))
        d.close()


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd in ("ec", "ce") and len(argv) == 4:
        cmd_build(cmd, argv[2], argv[3])
    elif cmd == "check" and len(argv) == 3:
        cmd_check(argv[2])
    elif cmd == "syllables":
        print("\n".join(syllable.SYLLABLES))
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
