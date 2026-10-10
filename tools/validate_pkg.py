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
# The OpenOrbis homebrew instructions explicitly require a 512x512 RGB PNG.
icon = Path("sce_sys/icon0.png").read_bytes()
if not icon.startswith(b"\\x89PNG\\r\\n\\x1a\\n"):
    raise SystemExit("FAIL: icon0.png is not PNG")
width, height, bit_depth, color_type = (
    *struct.unpack_from(">II", icon, 16), icon[24], icon[25]
)
if (width, height, bit_depth, color_type) != (512, 512, 8, 2):
    raise SystemExit(
        f"FAIL: icon must be 512x512 RGB24 no alpha, got {width}x{height} bit={bit_depth} color={color_type}"
    )
# Parse PS4 SFO keys instead of just looking for bytes somewhere in the file.
key_start, value_start, count = struct.unpack_from("<III", sfo, 8)
if 20 + count * 16 > len(sfo) or count > 100 or key_start >= len(sfo):
    raise SystemExit("FAIL: malformed param.sfo index")
def value_for(key):
    for i in range(count):
        offset = 20 + i * 16
        k_off, k_type, length, capacity, data_off = struct.unpack_from("<HHIII", sfo, offset)
        key_pos = key_start + k_off
        data_pos = value_start + data_off
        if key_pos >= len(sfo) or data_pos + length > len(sfo):
            raise SystemExit("FAIL: out-of-bounds SFO entry")
        name = sfo[key_pos:].split(b"\\0", 1)[0].decode("utf-8", "replace")
        if name == key:
            return sfo[data_pos:data_pos + length].split(b"\\0", 1)[0]
    return None
for key, desired in {
    "CATEGORY": b"gd",
    "TITLE_ID": b"ULFX00001",
    "CONTENT_ID": EXPECTED_CONTENT_ID.encode(),
    "APP_VER": b"04.00",
}.items():
    if value_for(key) != desired:
        raise SystemExit(f"FAIL: SFO {key} mismatch: {value_for(key)!r} != {desired!r}")
if not value_for("TITLE"):
    raise SystemExit("FAIL: app is missing an SFO TITLE")
print("PASS: PNG 512x512 RGB24 and mandatory SFO fields verified")
print(f"PASS: basic PS4 PKG structure/content ID/SFO/dependencies verified. size={size}; files={file_count}; entries={entry_count}")
print("IMPORTANT: cannot establish install or runtime reliability without testing on PS4 hardware.")
