# tools/i18n

| File | What | Usage |
|---|---|---|
| `game_texts.py` | The game's own texts of one `romfs/Mals` folder (SARC of MSBT) as label and text lines: the vocabulary a translation reuses | `uv run tools/i18n/game_texts.py EUde [-o DIR]` |
| `catalog.py` | The menu's text catalogs (`i18n/mkw.pot`, `i18n/<lang>.po`): merges the English template into every translation (changed English marked fuzzy), shows how far each language is, and generates the C arrays `build.py` compiles | `uv run tools/i18n/catalog.py update\|status\|gen DIR` |
| `fill_mod_strings.py` | One-shot fill of the Mods page strings in all catalog languages (author-provided, translators refine): only empty entries, never a translator's words | run once per new string batch, then `catalog.py status` to confirm |
