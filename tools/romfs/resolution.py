"""Write a RomFS overlay that sets MKW's internal render resolutions (GamePerformanceGraphicsConfigGParam).

Why: the game picks its 3D and screen resolutions from System/GymlArchive/general_format.Product_100.sarc.zs ->
Gyml/GamePerformanceGraphicsConfigGParam/30_Extendted...bgyml (BYML v7: Resolution{Low,Middle}{For3D,Screen}
[ForDiv,ForDiv4] = 1080/900/1440; it renders at 2560x1440). On a PC these are the knobs for GPU time and memory, read
by the game's own code, so no runtime scaling is needed. The integers are patched where they are (same sizes), the
archive is compressed again and listed in manifest.tsv for the runtime's overlay loader (runtime/sdk/fs/romfs.c).

Docked, the 3D scene renders at 65 % of the For3D lines whatever the GPU load (832x464 for 720, 1664x932 for 1440,
measured in races on 31/10/2026; IsEnableUpscale=0 does not change it); in handheld mode at all of them (For3D 831 drew
1476x828, measured on 13/11/2026, #195). --3d-internal LINES asks for that many rendered lines in the --mode the game
runs in (docked: 720 -> For3D 1108 -> a 1280x720 scene; handheld: For3D 720), as runtime/sdk/fs/romfs_res.c does.

Usage: uv run tools/romfs/resolution.py [--3d LINES | --3d-internal LINES [--mode docked|handheld]] [--screen LINES]
       [--upscale 0|1] [--out build/romfs-res]
Then run with MKW_ROMFS_PATCHES=<out>. Only the values given change.
"""

import argparse
import hashlib
import math
import os
import re
import struct
import sys
from compression import zstd
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/shaders"))
sys.path.insert(0, str(ROOT / "tools/prepare"))
from extract import sarc_members
from nup import Nup, is_nup, romfs_read

ARCHIVE_DIR = "System/GymlArchive"
ARCHIVE_NAME = re.compile(r"general_format\.Product_\d+\.sarc\.zs")  # the version's number: 100 in 1.0.0, 180 in 1.8.0
SCENE_SCALE = {"docked": 0.65, "handheld": 1.0}  # rendered scene lines / For3D lines (docked: 464 of 720, 932 of 1440)
MEMBER = "Gyml/GamePerformanceGraphicsConfigGParam/30_Extendted.game__GamePerformanceGraphicsConfigGParam.bgyml"


def archive(romfs: Path) -> str:
    """The RomFS path of the general_format archive of this copy (its name carries the game version, #195)."""
    names = (
        [p.rsplit("/", 1)[1] for p in Nup(romfs).files("romfs") if p.startswith(ARCHIVE_DIR + "/")]
        if is_nup(romfs)
        else [p.name for p in (romfs / ARCHIVE_DIR).iterdir()]
    )
    found = [n for n in names if ARCHIVE_NAME.fullmatch(n)]
    if len(found) != 1:
        sys.exit(f"resolution: {len(found)} general_format archives in {romfs}/{ARCHIVE_DIR}")
    return f"{ARCHIVE_DIR}/{found[0]}"


def byml_root_fields(b: bytes, at: int) -> dict[str, tuple[int, int]]:
    """{key: (entry offset in the archive, node type)} of the BYML at `at` (root dictionary and PostProcess)."""
    assert b[at : at + 2] == b"YB", "not a little-endian BYML"
    keys_off, _, root_off = struct.unpack_from("<III", b, at + 4)
    n = struct.unpack_from("<I", b, at + keys_off)[0] >> 8
    offs = struct.unpack_from(f"<{n}I", b, at + keys_off + 4)
    keys = [b[at + keys_off + o : b.index(0, at + keys_off + o)].decode() for o in offs]
    out = {}

    def walk(node: int, prefix: str) -> None:
        count = int.from_bytes(b[at + node + 1 : at + node + 4], "little")
        for k in range(count):
            e = at + node + 4 + 8 * k
            name, t = prefix + keys[int.from_bytes(b[e : e + 3], "little")], b[e + 3]
            if t == 0xC1:
                walk(struct.unpack_from("<I", b, e + 4)[0], name + ".")
            else:
                out[name] = (e + 4, t)

    walk(root_off, "")
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--romfs", type=Path, default=Path(os.environ.get("MKW_GAME", "game")) / "romfs")
    ap.add_argument("--3d", dest="r3d", type=int, help="lines of the 3D render (every *For3D* field)")
    ap.add_argument(
        "--3d-internal",
        dest="r3d_internal",
        type=int,
        help="lines the 3D scene is rendered at: For3D = LINES / 0.65 docked, = LINES in handheld mode",
    )
    ap.add_argument(
        "--mode",
        choices=SCENE_SCALE,
        default=os.environ.get("MKW_OPERATION_MODE") or "docked",
        help="the operation mode the game runs in, for --3d-internal (default: MKW_OPERATION_MODE, else docked)",
    )
    ap.add_argument("--screen", type=int, help="lines of the screen (2D) render (every *Screen* field)")
    ap.add_argument("--upscale", type=int, choices=(0, 1), help="PostProcess.IsEnableUpscale")
    ap.add_argument("--out", type=Path, default=ROOT / "build/romfs-res")
    args = ap.parse_args()
    if args.r3d_internal:
        args.r3d = math.ceil(args.r3d_internal / SCENE_SCALE[args.mode])
    arc = archive(args.romfs)
    orig = romfs_read(args.romfs, arc)
    sarc = bytearray(zstd.decompress(orig))
    member = sarc_members(bytes(sarc))[MEMBER]
    at = bytes(sarc).index(member)
    for name, (off, t) in sorted(byml_root_fields(bytes(sarc), at).items()):
        new = None
        if args.r3d and "For3D" in name and t in (0xD1, 0xD3):
            new = args.r3d
        elif args.screen and "Screen" in name and t in (0xD1, 0xD3):
            new = args.screen
        elif args.upscale is not None and name == "PostProcess.IsEnableUpscale" and t == 0xD0:
            new = args.upscale
        if new is not None:
            print(f"{name}: {struct.unpack_from('<I', sarc, off)[0]} -> {new}")
            struct.pack_into("<I", sarc, off, new)
    data = zstd.compress(bytes(sarc), 19)
    dest = args.out / arc
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
    line = f"{arc}\t{len(data)}\t{hashlib.sha256(data).hexdigest()}\t{hashlib.sha256(orig).hexdigest()}\n"
    (args.out / "manifest.tsv").write_text(line)
    print(f"{dest} ({len(data)} bytes); run with MKW_ROMFS_PATCHES={args.out}")


if __name__ == "__main__":
    main()
