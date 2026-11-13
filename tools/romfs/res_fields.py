"""Print every scalar field of GamePerformanceGraphicsConfigGParam in the original archive and in an overlay."""
import os
import struct
import sys
from compression import zstd
from pathlib import Path

sys.path.insert(0, "tools/romfs"); sys.path.insert(0, "tools/shaders"); sys.path.insert(0, "tools/prepare")
from extract import sarc_members
from nup import romfs_read
from resolution import MEMBER, archive, byml_root_fields


def fields_of(data):
    s = zstd.decompress(data); m = sarc_members(s)[MEMBER]; at = s.index(m)
    out = {}
    for k, (off, t) in byml_root_fields(s, at).items():
        v = struct.unpack_from("<I", s, off)[0]
        if t == 0xD2: v = round(struct.unpack_from("<f", s, off)[0], 4)
        out[k] = (t, v)
    return out
ROMFS = Path(os.environ.get("MKW_GAME", "game")) / "romfs"
a = fields_of(romfs_read(ROMFS, archive(ROMFS)))
b = fields_of((Path("build/romfs-res") / archive(ROMFS)).read_bytes())
for k in sorted(a): print(f"{k:60s} {a[k][0]:#x} {a[k][1]!s:>10} {'-> ' + str(b[k][1]) if b[k] != a[k] else ''}")
