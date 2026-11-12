# tools/mod

Mod import for the start screen's Mods page (`runtime/ui/launcher/mods.cpp` runs `import.py` the way
`launcher_setup.cpp` runs `tools/prepare/prepare.py`: the package's Python, one event per line).

| File | What it is |
|---|---|
| `import.py` | Copies a Switch-style mod (zip or folder, top folder is the mod: `romfs/` and/or `exefs/`) into `<game>/mods/<name>/`, writes the `romfs/manifest.tsv` the overlay loader reads and a `mod.json` the Mods page shows |
