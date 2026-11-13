"""prepare.py's copies of the player's game into the game folder: loose files COPY_JOBS at a time, resumable, and the
files the copied version does not have removed.

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


def own_dir(d: Path, src: Path) -> bool:
    """d a folder of the game folder's own, not a link to another (the player's copy, a developer's game/romfs), nor
    one holding src (a package kept in game/romfs, the game folder as its own source): only those lose files."""
    if not d.is_dir() or d.is_symlink() or os.path.isjunction(d):
        return False
    return not src.resolve().is_relative_to(d.resolve())


def remove_stale(dst: Path, src: Path, kept: set[str], version: str) -> None:
    """dst's files that kept (paths under dst, with /) does not have removed, then the folders that left empty: a copy
    over a folder set up from another version leaves that version's files behind (1.0.0's general_format.Product_100
    among 1.8.0's Product_180), and tools that look for the game's own files then find two: the resolution overlay
    built from the stale one, mods checked against it (#261)."""
    if not own_dir(dst, src):
        return
    removed, dirs = 0, []
    for d, subdirs, files in os.walk(dst, topdown=False):
        dirs += [Path(d, s) for s in subdirs]
        for f in files:
            p = Path(d, f)
            if p.relative_to(dst).as_posix() not in kept:
                p.unlink()
                removed += 1
    for d in dirs:
        try:
            d.rmdir()  # only when the pruning emptied it
        except OSError:
            pass
    if removed:
        print(f"copy: removed {removed} stale files that {version} does not have from {dst}", file=sys.stderr)


def clear_dir(d: Path, src: Path) -> None:
    """d emptied before a copy from src that writes every file anyway (mkw --unpack of a .nx), so no other version's
    stay."""
    if own_dir(d, src):
        shutil.rmtree(d)
        print(f"copy: {d} emptied before unpacking {src}", file=sys.stderr)


def copy_tree(src: Path, dst: Path, total: int, version: str = "") -> None:
    """src into dst with progress, COPY_JOBS files at a time: the RomFS's 205 660 files one by one were most of a
    copy's time on Windows, where creating a file costs more than its bytes. With version, then the files src does
    not have are removed (remove_stale), so the folder is src's."""
    done = 0
    kept: set[str] = set()
    with ThreadPoolExecutor(COPY_JOBS) as pool:
        pending = []
        for d, _, files in os.walk(src):
            out = dst / Path(d).relative_to(src)
            out.mkdir(parents=True, exist_ok=True)
            pending += [pool.submit(copy_one, Path(d, f), out / f) for f in files]
            kept.update(Path(d, f).relative_to(src).as_posix() for f in files)
        for fut in as_completed(pending):
            fut.result()
            done += 1
            if done % 500 == 0 or done == total:
                emit("progress", done, total)
    if version:
        remove_stale(dst, src, kept, version)
