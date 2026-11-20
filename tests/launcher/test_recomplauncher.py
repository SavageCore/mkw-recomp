"""recomplauncher.json (issue #706) stays shippable and in step with the settings table.

Why: RecompLauncher reads this file to set up, start and configure the game, and the guide asks to keep it in step
with the settings when keys are added or renamed. The setup it runs is tools/prepare/rl_prepare.py (prepare.py with
its events translated to progress lines), packed next to the program by tools/build (dist.py into the player's folder,
release.py into the AppImage and the Windows zip). Without the game or a GPU: the JSON against the schema's shape,
every key it writes against runtime/core/base/cfg.c (known keys, defaults, accepted values), prepare's version against
prepare.py's PREP_VERSION, and the packers against naming the file.

Usage: uv run pytest tests/launcher/test_recomplauncher.py
"""

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RL = json.loads((ROOT / "recomplauncher.json").read_text())
CFG = (ROOT / "runtime/core/base/cfg.c").read_text()


def cfg_tables() -> tuple[set[str], dict[str, tuple[str, str | None]]]:
    """The known keys and the settings table (key -> default, accepted or None for a free value)."""
    known_src = CFG.split("static const char *const known[] = {", 1)[1].split("};", 1)[0]
    known = set(re.findall(r'"([A-Z0-9_]+)"', known_src))
    metas_src = CFG.split("static const CfgMeta metas[] = {", 1)[1].split("};", 1)[0]
    metas = {}
    for m in re.finditer(r'\{"([A-Z0-9_]+)", "([^"]*)", (?:NULL|"([^"]*)")\s*,', metas_src):
        metas[m.group(1)] = (m.group(2), m.group(3))
    return known, metas


KNOWN, METAS = cfg_tables()


def written() -> dict[str, str]:
    """Every key the file writes (choices' set, sliders' key, config ensure/always/new/game_path)."""
    keys: dict[str, str] = {}
    for entry in RL.get("config", []):
        for group in ("ensure", "always", "new"):
            for k, v in entry.get(group, {}).items():
                keys[k] = v
        if "game_path" in entry:
            keys[entry["game_path"]] = "<the player's copy>"
    for s in RL["settings"]:
        for c in s.get("choices", []):
            for k, v in c.get("set", {}).items():
                keys[k] = v
        if "slider" in s:
            keys[s["slider"]["key"]] = "<slider>"
    return keys


def test_shape() -> None:
    assert RL["format"] == 1
    assert RL["start"]["program"]
    assert RL["prepare"]["program"] and RL["prepare"]["args"] and RL["prepare"]["done"]
    assert int(RL["prepare"]["timeout"]) >= 3600  # a first setup compiles the whole game
    ids = [s["id"] for s in RL["settings"]]
    assert len(ids) == len(set(ids)), "settings ids are unique"
    for s in RL["settings"]:
        assert s["label"] and (len(s.get("choices", [])) >= 2 or "slider" in s or "bindings" in s)


def test_keys_known() -> None:
    for k in written():
        assert k.upper() in KNOWN, f"{k}: not a key the runtime reads (runtime/core/base/cfg.c)"


def test_defaults_match() -> None:
    for s in RL["settings"]:
        for k, v in s.get("defaults", {}).items():
            assert METAS[k.upper()][0] == v, f"{k}: defaults {v!r} is not the setting's own {METAS[k.upper()][0]!r}"


def test_choices_within_accepted() -> None:
    for s in RL["settings"]:
        for c in s.get("choices", []):
            for k, v in c.get("set", {}).items():
                accepted = METAS[k.upper()][1]
                if accepted is None or ".." in accepted:
                    continue  # a free value (a path, an address) or a numeric range
                assert v == "" or v in accepted.split("|"), f"{k}={v!r}: not one of {accepted}"


def test_slider_ranges() -> None:
    for s in RL["settings"]:
        if "slider" not in s:
            continue
        accepted = METAS[s["slider"]["key"].upper()][1]
        assert accepted is not None and ".." in accepted
        lo, hi = (float(x) for x in accepted.split(".."))
        assert (s["slider"]["min"], s["slider"]["max"]) == (lo, hi)


def test_prepare_version_tracks_prep() -> None:
    (prep,) = re.findall(r"^PREP_VERSION = (\d+)", (ROOT / "tools/prepare/prepare.py").read_text(), re.M)
    assert RL["prepare"]["version"] == prep, "bump prepare.version with PREP_VERSION (every setup runs again)"


def test_packed_next_to_the_program() -> None:
    for script in ("tools/build/dist.py", "tools/build/release.py"):
        assert "recomplauncher.json" in (ROOT / script).read_text(), f"{script} does not pack the file"
