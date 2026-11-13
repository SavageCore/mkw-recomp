# /// script
# requires-python = ">=3.12"
# dependencies = ["pillow==11.3.0"]
# ///
"""Finds the draw program behind a coloured artifact by replaying one frame capture with half of the candidate
programs skipped each time (MKW_SKIP_PROGRAMS takes key prefixes), until one key prefix is left.

Why: artifacts that only show on GPUs we do not have (#174, Intel ANV: red halos and a red tint on the 3D scene) cannot
be narrowed here, and a game run per guess costs the reporter minutes. A capture replays in seconds on their machine,
and about 35 replays (some 20 min) name the program, which we can then read here (`<game>/spirv/<key>.spv`).

The artifact is measured as the pixels of the given colour in the last present of the replay (--color red counts
R >= 140 with R above 1.8 times G and B). A half is the culprit when skipping it removes at least --drop of them
(default 60 %) while keeping half of the other lit pixels (a half that blanks the picture, as the final composite does,
is not it). Skipping draws also skips what they cover, so a half that removes the ground under a red glow counts
too: the final program is checked alone and printed with what it removed. If neither half removes it, the artifact
does not come from one draw program (a compute pass, or several programs): the script says so and stops.

Capture first, on the GPU that shows the artifact (Linux), with the scene on screen at the given present:
    MKW_CAPTURE=1 MKW_CAPTURE_AT=2000 MKW_CAPTURE_DIR=~/mkw-cap ./mkw
Usage: uv run tools/test/skip_bisect.py CAPTURE_DIR [--mkw PATH] [--color red] [--drop 0.6] [--out DIR]
       (--mkw: build/mkw from a source build; from the AppImage, ~/.local/share/mkw-recomp/mkw)
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image

HEX = "0123456789abcdef"  # 16 one-digit prefixes at most: MKW_SKIP_PROGRAMS keeps 16 entries (vk_exec_draw.c)
PREFIX_DIGITS = 5  # 20 bits: about a million prefixes for the ~23 000 programs of a version


def is_red(r: int, g: int, b: int) -> bool:
    return r >= 140 and r > 1.8 * g and r > 1.8 * b


COLORS = {"red": is_red}


class Replayer:
    def __init__(self, mkw: Path, capture: Path, out: Path, color: str) -> None:
        self.mkw, self.capture, self.out, self.match = mkw, capture, out, COLORS[color]
        self.n = 0

    def count(self, skip: list[str]) -> tuple[int, int]:
        """Pixels of the colour and the other lit pixels in the last present, replayed with these prefixes skipped."""
        self.n += 1
        d = self.out / f"r{self.n:02d}"
        env = dict(os.environ)
        env.pop("MKW_SKIP_PROGRAMS", None)
        if skip:
            env["MKW_SKIP_PROGRAMS"] = ",".join(skip)
        with open(self.out / f"r{self.n:02d}.log", "w") as log:
            r = subprocess.run([str(self.mkw), "--replay", str(self.capture), "--out", str(d), "--tag", "f"],
                               env=env, stdout=log, stderr=subprocess.STDOUT)  # fmt: skip
        pngs = sorted(d.glob("*.png"), key=lambda p: p.stat().st_mtime)
        if r.returncode or not pngs:
            sys.exit(f"replay {self.n} failed (exit {r.returncode}): {self.out}/r{self.n:02d}.log")
        img = Image.open(pngs[-1]).convert("RGB")
        n = rest = 0
        for px in img.get_flattened_data():
            if self.match(*px):
                n += 1
            elif max(px) >= 24:
                rest += 1
        print(f"  replay {self.n:2d}: {n:8d} px, {rest:8d} other  skipping {','.join(skip) or '(nothing)'}", flush=True)
        return n, rest


def bisect(rp: Replayer, base: tuple[int, int], drop: float) -> str | None:
    """The key prefix whose draws hold the artifact, or None when no single half does. A half that blanks most of the
    rest of the picture too (the final composite: build/cap/title, 13/11/2026) is not taken for it."""
    gone, keep = base[0] * (1 - drop), base[1] * 0.5

    def removes(half: list[str]) -> bool:
        n, rest = rp.count(half)
        return n <= gone and rest >= keep

    cands = list(HEX)
    while True:
        while len(cands) > 1:
            half_a, half_b = cands[: len(cands) // 2], cands[len(cands) // 2 :]
            if removes(half_a):
                cands = half_a
            elif removes(half_b):
                cands = half_b
            else:
                print(f"neither half of {cands[0]}..{cands[-1]} removes it: not one draw program (compute, or several)")
                return None
        if len(cands[0]) >= PREFIX_DIGITS:
            return cands[0]
        cands = [cands[0] + h for h in HEX]


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("capture", type=Path)
    p.add_argument("--mkw", type=Path, default=Path(__file__).resolve().parents[2] / "build/mkw")
    p.add_argument("--color", choices=sorted(COLORS), default="red")
    p.add_argument("--drop", type=float, default=0.6, help="share of the pixels a culprit half removes")
    p.add_argument("--out", type=Path, help="replays and logs (default CAPTURE/bisect)")
    a = p.parse_args()
    out = a.out or a.capture / "bisect"
    out.mkdir(parents=True, exist_ok=True)
    rp = Replayer(a.mkw, a.capture, out, a.color)
    base = rp.count([])
    if base[0] < 200:
        sys.exit(f"only {base[0]} {a.color} pixels in the last present: capture a frame that shows the artifact")
    prefix = bisect(rp, base, a.drop)
    if prefix is None:
        sys.exit(1)
    alone = rp.count([prefix])
    print(
        f"\nprogram key prefix {prefix}: skipping it alone leaves {alone[0]} of {base[0]} {a.color} pixels and "
        f"{alone[1]} of {base[1]} others"
    )
    print(f"the program: <folder of mkw>/spirv/{prefix}*.spv")


if __name__ == "__main__":
    main()
