"""Import a Switch-style mod into <game>/mods for the start screen's Mods page.

Why: mods ship as Eglantine/Ryujinx layout (top folder is the mod: romfs/ and/or exefs/ with
<build id>.ips + .pchtxt inside), but the port reads a mod from <game>/mods/<name>/ with a manifest.tsv
over its romfs/ (runtime/sdk/fs/romfs.c) and a mod.json the Mods page shows. This copies the mod in,
checks the exefs patches name this game's build id, and writes both files.

Usage: import.py --game EXEDIR --romfs ROMFSDIR [--exefs DIR] [--build-id HEX] (--zip FILE | --folder DIR) [--name NAME]
  --game: the folder of mkw / mkw.exe (mods/ is made inside it)
  --romfs: the loose RomFS the manifest's original hashes come from
  --zip: a mod zip (its top folder is the mod; --name renames it)
  --folder: an extracted mod folder (its name, or --name)
  Prints "done <name>" with the mod's file counts, or "error <code> <detail>"; exit 0 done, 1 failed,
  2 bad command line. Codes: layout (no romfs/ or exefs/ inside), name (nothing usable), io (a copy
  or hash failed). An exefs .ips that names another build id is kept and listed as incompatible.
"""

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
import zipfile

BUILD_ID_OFF, BUILD_ID_LEN = 0x40, 8


def fail(code, detail):
    print(f"error {code} {detail}", flush=True)
    return 1


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def game_build_id(exefs_main):
    with open(exefs_main, "rb") as f:
        f.seek(BUILD_ID_OFF)
        return f.read(BUILD_ID_LEN).hex()


def clean_name(name):
    keep = "".join(c if c.isalnum() or c in ("-", "_", " ") else "_" for c in name).strip()
    return keep.strip("._")[:64]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--game", required=True)
    ap.add_argument("--romfs", required=True)
    ap.add_argument("--zip", dest="zip_path")
    ap.add_argument("--folder", dest="folder")
    ap.add_argument("--name", default="")
    ap.add_argument("--exefs", default="",
                    help="the game's exefs folder (default: <game>/exefs); its main names the build id")
    ap.add_argument("--build-id", default="",
                    help="16 hex chars of exefs/main's build id, instead of reading it from the exefs")
    args = ap.parse_args()
    if bool(args.zip_path) == bool(args.folder):
        print("error usage: one of --zip or --folder", flush=True)
        return 2
    mods_dir = os.path.join(args.game, "mods")
    loose = args.romfs
    if not os.path.isdir(loose):
        return fail("romfs", f"{loose} is not a loose romfs folder")
    try:
        exefs = args.exefs or os.path.join(args.game, "exefs")
        game_id = args.build_id.lower() if args.build_id else game_build_id(os.path.join(exefs, "main"))
    except OSError as e:
        return fail("nogame", f"{e.filename or args.game}")
    if len(game_id) != 16 or any(c not in "0123456789abcdef" for c in game_id):
        return fail("usage", "bad --build-id")

    with tempfile.TemporaryDirectory(prefix="mkwmod") as tmp:
        if args.zip_path:
            try:
                with zipfile.ZipFile(args.zip_path) as z:
                    tops = {n.split("/")[0] for n in z.namelist() if "/" in n and not n.startswith("__MACOSX")}
                    if len(tops) != 1:
                        return fail("layout", f"{len(tops)} top folders, want 1")
                    z.extractall(tmp)
                    top = next(iter(tops))
                    src = os.path.join(tmp, top)
            except (zipfile.BadZipFile, OSError) as e:
                return fail("io", str(e))
            default = top
        else:
            if not os.path.isdir(args.folder):
                return fail("layout", "not a folder")
            # An extracted mod is either the mod folder itself or one folder holding it (a zip's extraction).
            inner = [d for d in os.listdir(args.folder)
                     if os.path.isdir(os.path.join(args.folder, d)) and not d.startswith(".")]
            has_direct = os.path.isdir(os.path.join(args.folder, "romfs")) or \
                os.path.isdir(os.path.join(args.folder, "exefs"))
            if has_direct:
                src, default = args.folder, os.path.basename(os.path.normpath(args.folder))
            elif len(inner) == 1:
                src, default = os.path.join(args.folder, inner[0]), inner[0]
            else:
                return fail("layout", "no romfs/ or exefs/ inside")
        has_romfs = os.path.isdir(os.path.join(src, "romfs"))
        has_exefs = os.path.isdir(os.path.join(src, "exefs"))
        if not has_romfs and not has_exefs:
            return fail("layout", "no romfs/ or exefs/ inside")

        name = clean_name(args.name) if args.name else clean_name(default)
        if not name:
            return fail("name", "empty after cleaning")
        shipped_patches = {}
        try:
            with open(os.path.join(src, "mod.json"), encoding="utf-8") as _m:
                shipped_patches = json.load(_m).get("patches", {}) or {}
        except (OSError, ValueError):
            pass
        usable = [p for p in shipped_patches.get("spans", []) + shipped_patches.get("sites", []) if isinstance(p, dict)]
        if has_exefs and not usable:
            return fail("notported", f"{default}: code not ported (mod.json has no patches for this game)")
        dest = os.path.join(mods_dir, name)
        if os.path.lexists(dest):
            return fail("name", f"{name} already imported")
        shutil.copytree(src, dest, symlinks=False)

        builds, matching = [], False
        if has_exefs:
            for fn in sorted(os.listdir(os.path.join(dest, "exefs"))):
                if fn.endswith(".ips"):
                    bid = fn[:-4].lower()
                    builds.append(bid)
                    # the file names the full build id, the game its first 8 bytes (recomp/versions.py)
                    matching = matching or bid.startswith(game_id) or game_id.startswith(bid)
        n_romfs = 0
        if has_romfs:
            lines = []
            for root, _, files in os.walk(os.path.join(dest, "romfs")):
                for fn in sorted(files):
                    full = os.path.join(root, fn)
                    rel = os.path.relpath(full, os.path.join(dest, "romfs")).replace(os.sep, "/")
                    if rel == "manifest.tsv":
                        continue
                    size = os.path.getsize(full)
                    orig = os.path.join(loose, rel.replace("/", os.sep))
                    osha = sha256_file(orig) if os.path.isfile(orig) else "-"
                    lines.append(f"{rel}\t{size}\t{sha256_file(full)}\t{osha}\n")
                    n_romfs += 1
            with open(os.path.join(dest, "romfs", "manifest.tsv"), "w", encoding="utf-8",
                       newline="\n") as m:
                m.writelines(lines)
        # a shipped mod declares its port in mod.json (title, switches, patches); import keeps them
        shipped, switches = {}, {}
        try:
            with open(os.path.join(src, "mod.json"), encoding="utf-8") as m:
                shipped = json.load(m)
                switches = shipped.get("switches", {}) or {}
        except (OSError, ValueError):
            pass
        # the name the player typed (or a renamed folder) is what the row shows; the shipped title only
        # stands in when the mod keeps its own folder name
        title = name if (args.name or default != name) else (shipped.get("title") or name)
        meta = {"name": name, "title": title, "has_romfs": has_romfs,
                "romfs_files": n_romfs, "has_exefs": has_exefs, "exefs_builds": builds,
                "exefs_match": matching if has_exefs else True, "switches": switches}
        if shipped.get("patches"):
            meta["patches"] = shipped["patches"]
        with open(os.path.join(dest, "mod.json"), "w", encoding="utf-8", newline="\n") as m:
            json.dump(meta, m, indent=2)
            m.write("\n")
    try:
        print(f"done {name} romfs={n_romfs} exefs={','.join(builds) if builds else '-'}", flush=True)
    except BrokenPipeError:
        os._exit(0)   # the mod is written; the menu went away mid-print
    return 0


if __name__ == "__main__":
    sys.exit(main())
