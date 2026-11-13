# tools/romfs

RomFS overlays (game files replaced when read; `runtime/sdk/fs/romfs.c`, `MKW_ROMFS_PATCHES`).

| Script | What it does | Usage |
|---|---|---|
| `resolution.py` | Changes the game's internal resolutions (`GamePerformanceGraphicsConfigGParam`: 3D and screen, 1080/900/1440 in 1.0.0) and `IsEnableUpscale` in `general_format.Product_100.sarc.zs`, and writes the overlay with its `manifest.tsv` | `uv run tools/romfs/resolution.py --3d-internal 720 --screen 720` (scene at 1280x720: the game draws the 3D at 65 % of `For3D`, which ends up at 1 108) → `MKW_ROMFS_PATCHES=build/romfs-res` (the run scripts). The player picks it with `MKW_RESOLUTION` in the Display tab, which the runtime applies the same way (`runtime/sdk/fs/romfs_res.c`) |
| `byml.py` | Minimal BYML reader (v2-v7, little- or big-endian, with v7 hash dictionaries) that prints the tree, e.g. `GameData/GameDataList` | `python3 tools/romfs/byml.py FILE` |
| `findbytes.py` | Searches byte sequences across the whole RomFS, decompressing nested zstd | see its header |
| `res_fields.py` | Fields of `GamePerformanceGraphicsConfigGParam` in the original and in an overlay | see its header |
