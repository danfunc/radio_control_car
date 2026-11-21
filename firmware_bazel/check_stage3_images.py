#!/usr/bin/env python3
"""stage3 構成の車の成果物を検査する (実機なし。形だけの確認)。

  check_stage3_images.py --stage3 stage3.bin --app car_app.bin --uf2 car_full.uf2

検査する内容:
  1. app.bin: 先頭が ARM ベクタ表 (SP が SRAM 内、reset が Thumb かつ 0x1000A000 以降の app 内)、
     先頭 4 KiB 以内に IMAGE_DEF ブロック (RP2350 の PICOBIN) があり、984 KiB に収まる。
  2. UF2: 全ブロックの family が 0xE48BFF59。書込みアドレスは prefix (0x10000000-0x10007FFF)、
     journal (0x10008000-0x10009FFF)、app (0x1000A000 以降 984 KiB 内) のどれかに収まり、
     FS / staging / BT の領域 (0x10100000 以降) には触れない。
  3. UF2 を平らに戻したとき、prefix 先頭が stage3.bin、app 領域が app.bin と一致する。
終了コード 0 = 全部 OK。失敗は理由を stderr に出して 1。
値の写し元は XNO tools/stage3/provision_rp2350.py (layout_rp2350.hpp と同値)。
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

XIP = 0x10000000
PREFIX_END = 0x8000          # prefix: 0x0000-0x7FFF (manifest ページ 0x7F00 を含む)
JOURNAL_OFF, JOURNAL_END = 0x8000, 0xA000
APP_OFF, APP_END = 0xA000, 0x100000  # 984 KiB
SRAM_BASE, SRAM_END = 0x20000000, 0x20082000
FAMILY = 0xE48BFF59
UF2_MAGIC = (0x0A324655, 0x9E5D5157, 0x0AB16F30)
IMAGE_DEF_START = 0xFFFFDED3  # RP2350 PICOBIN block marker


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(1)


def check_app(app: bytes) -> None:
    if not app or len(app) > APP_END - APP_OFF:
        fail(f"app.bin size {len(app)} not in 1..{APP_END - APP_OFF}")
    sp, reset = struct.unpack_from("<II", app, 0)
    if not (SRAM_BASE < sp <= SRAM_END):
        fail(f"initial SP 0x{sp:08X} is outside SRAM")
    if not (reset & 1):
        fail(f"reset vector 0x{reset:08X} is not Thumb")
    r = reset & ~1
    lo, hi = XIP + APP_OFF, XIP + APP_OFF + len(app)
    if not (lo <= r < hi):
        fail(f"reset vector 0x{reset:08X} not inside app [0x{lo:08X},0x{hi:08X}); not linked at 0x{lo:08X}?")
    magic = struct.pack("<I", IMAGE_DEF_START)
    if app.find(magic, 0, 4096) < 0:
        fail("no IMAGE_DEF block within the first 4 KiB of app.bin")
    print(f"app: {len(app)} bytes, SP=0x{sp:08X}, reset=0x{reset:08X} (in app at 0x{lo:08X})")


def parse_uf2(raw: bytes):
    if len(raw) % 512 or not raw:
        fail("UF2 size is not a multiple of 512")
    blocks = []
    for off in range(0, len(raw), 512):
        m0, m1, flags, addr, size, _n, _tot, fam = struct.unpack_from("<8I", raw, off)
        (end,) = struct.unpack_from("<I", raw, off + 508)
        if (m0, m1, end) != UF2_MAGIC:
            fail(f"bad UF2 magic at block {off // 512}")
        if not (flags & 0x2000) or fam != FAMILY:
            fail(f"block {off // 512}: family 0x{fam:08X} flags 0x{flags:X}; want 0x{FAMILY:08X}")
        blocks.append((addr, raw[off + 32:off + 32 + size]))
    return blocks


def check_uf2(raw: bytes, stage3: bytes, app: bytes) -> None:
    blocks = parse_uf2(raw)
    flat = {}
    spans = {"prefix": 0, "journal": 0, "app": 0}
    for addr, data in blocks:
        off = addr - XIP
        end = off + len(data)
        if addr < XIP or end > APP_END:
            fail(f"block at 0x{addr:08X}+{len(data)} is outside prefix/journal/app (touches FS/staging/BT?)")
        for part, (lo, hi) in {"prefix": (0, PREFIX_END), "journal": (JOURNAL_OFF, JOURNAL_END),
                               "app": (APP_OFF, APP_END)}.items():
            if lo <= off and end <= hi:
                spans[part] += len(data)
                break
        else:
            fail(f"block at 0x{addr:08X}+{len(data)} straddles regions")
        for i, b in enumerate(data):
            flat[off + i] = b
    for part, n in spans.items():
        if n == 0:
            fail(f"no data written to {part}")
    for i, b in enumerate(stage3):
        if flat.get(i) != b:
            fail(f"prefix byte 0x{i:X} differs from stage3.bin")
    for i, b in enumerate(app):
        if flat.get(APP_OFF + i) != b:
            fail(f"app byte 0x{i:X} differs from app.bin")
    print(f"uf2: {len(blocks)} blocks, family 0x{FAMILY:08X}, prefix={spans['prefix']} "
          f"journal={spans['journal']} app={spans['app']} bytes; contents match stage3.bin / app.bin")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stage3", required=True, type=Path)
    ap.add_argument("--app", required=True, type=Path)
    ap.add_argument("--uf2", required=True, type=Path)
    a = ap.parse_args()
    app = a.app.read_bytes()
    check_app(app)
    check_uf2(a.uf2.read_bytes(), a.stage3.read_bytes(), app)
    print("OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
