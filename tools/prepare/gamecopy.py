"""prepare.py's copies of the player's game into the game folder: loose files COPY_JOBS at a time, resumable.

Why: its own module so prepare.py keeps the steps; imported by tools/prepare/prepare.py, nothing to run by hand.
"""

import os
import shutil
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

from machine import emit

COPY_JOBS = 6  # files copied at once: NTFS creates ~170 small files a second from one stream, ~460 from six (Windows test PC, 08/11/2026)


def copy_one(s: Path, t: Path) -> None:
    """One file, unless already there with the same size (resume); through .part, so a cut copy is never taken."""
    if t.is_file() and t.stat().st_size == s.stat().st_size:
        return
    part = t.with_name(t.name + ".part")
    shutil.copyfile(s, part)
    part.replace(t)


def copy_tree(src: Path, dst: Path, total: int, prune: str = "") -> None:
    """src into dst with progress, COPY_JOBS files at a time: the RomFS's 205 660 files one by one were most of a
    copy's time on Windows, where creating a file costs more than its bytes.

    prune (the game version): a copy over a folder set up from another version leaves that version's files behind
    (1.0.0's general_format.Product_100 among 1.8.0's Product_180), and tools that look for the game's own files then
    find two: the resolution overlay built from the stale one, mods checked against it. The files src does not have
    are removed, so the folder is src's."""
    done = 0
    kept: set[str] = set()
    with ThreadPoolExecutor(COPY_JOBS) as pool:
        pending = []
        for d, _, files in os.walk(src):
            out = dst / Path(d).relative_to(src)
            out.mkdir(parents=True, exist_ok=True)
            pending += [pool.submit(copy_one, Path(d, f), out / f) for f in files]
            kept.update(str(Path(d, f).relative_to(src)) for f in files)
        for fut in as_completed(pending):
            fut.result()
            done += 1
            if done % 500 == 0 or done == total:
                emit("progress", done, total)
    if not prune or not dst.is_dir():
        return
    removed, dirs = 0, []
    for d, subdirs, files in os.walk(dst, topdown=False):
        dirs += [Path(d, s) for s in subdirs]
        for f in files:
            p = Path(d, f)
            if str(p.relative_to(dst)) in kept or not p.is_file():
                continue
            p.unlink()
            removed += 1
    for d in dirs:
        try:
            d.rmdir()  # only when the pruning emptied it
        except OSError:
            pass
    if removed:
        print(f"copy: removed {removed} stale files that {prune} does not have", file=sys.stderr)
        emit("removed", removed, prune)
