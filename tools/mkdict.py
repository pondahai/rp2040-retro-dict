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
    t0 = time.time()
    mod = build_ec if kind == "ec" else build_ce
    stats = mod.build(src, idx, dat)
    print("%s: %d 筆，索引 %d bytes，內文 %d bytes，耗時 %.1fs"
          % (name, stats["entries"],
             os.path.getsize(idx), stats["dat_bytes"], time.time() - t0))
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
        d = C.Dictionary(idx, dat)
        print("%s: %d 筆 source=%s" % (name, d.hdr.rec_count, d.hdr.source_tag))
        d.src.reads = 0
        hits = d.lookup(norm(probe))
        print("   查 %-6s 命中 %d 筆，SD 讀取 %d 次"
              % (probe, len(hits), d.src.reads))
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
