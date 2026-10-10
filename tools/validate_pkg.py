#!/usr/bin/env python3
"""Fail the build when a generated PS4 package has invalid basic structure.

This is a static preflight, NOT proof the package will launch on real PS4 hardware.
"""
from pathlib import Path
import struct
import sys

EXPECTED_CONTENT_ID = "IV0000-ULFX00001_00-USBLinkFixer0001"
pkg = Path(sys.argv[1] if len(sys.argv)>1 else f"{EXPECTED_CONTENT_ID}.pkg")
if not pkg.is_file():
    raise SystemExit(f"FAIL: missing PKG {pkg}")
size = pkg.stat().st_size
if size < 0x4000:
    raise SystemExit(f"FAIL: suspicious PKG size {size}")
with pkg.open("rb") as file:
    header = file.read(0x1000)
if header[:4] != b"\x7fCNT":
    raise SystemExit(f"FAIL: invalid PS4 PKG header: {header[:4].hex()}")
content_id = header[0x40:0x64].split(b"\0", 1)[0].decode("ascii", "replace")
if content_id != EXPECTED_CONTENT_ID:
    raise SystemExit(f"FAIL: wrong PKG content ID {content_id!r}")
file_count = struct.unpack_from(">I",header,0x0C)[0]
entry_count = struct.unpack_from(">I",header,0x10)[0]
if not (1 <= file_count <= 10000 and 1 <= entry_count <= 10000):
    raise SystemExit(f"FAIL: implausible PKG entries file_count={file_count}, entries={entry_count}")
for name in ["eboot.bin","sce_sys/param.sfo","sce_sys/icon0.png",
             "sce_sys/about/right.sprx","sce_module/libc.prx",
             "sce_module/libSceFios2.prx"]:
    p=Path(name)
    if not p.is_file() or p.stat().st_size == 0:
        raise SystemExit(f"FAIL: required game content missing: {p}")
sfo=Path("sce_sys/param.sfo").read_bytes()
if not sfo.startswith(b"\0PSF"):
    raise SystemExit("FAIL: invalid param.sfo header")
if b"ULFX00001" not in sfo or EXPECTED_CONTENT_ID.encode() not in sfo:
    raise SystemExit("FAIL: incorrect title ID or content ID in param.sfo")
print(f"PASS: basic PS4 PKG structure/content ID/SFO/dependencies verified. size={size}; files={file_count}; entries={entry_count}")
print("IMPORTANT: cannot establish install or runtime reliability without testing on PS4 hardware.")
