"""Run the setup for RecompLauncher, translating its events to progress lines.

Why: prepare.py prints one event per line for the start screen ("step check",
"progress 128 4096", "skip romfs", "done", "error code detail"), while
RecompLauncher's prepare step reads "progress <0..1> <text>" lines. This runs
prepare.py with the same interpreter and translates (the steps keep rough weights;
only the bar reads them, an interrupted setup still resumes from setup-state.json).
It is the prepare entry of recomplauncher.json: prepare.program runs the package's
bundled interpreter with this file in its args, shipped in setup/src like the
prepare tools it runs (SOURCES in tools/build/dist.py holds tools/prepare whole).

Usage: python -I rl_prepare.py --source DIR|FILE --game DIR [--romfs copy|link]
Exit: 0 set up, 1 failed (prepare.py's own error line is already on stderr).
"""

import argparse
import subprocess
import sys
from pathlib import Path

STEPS = ("check", "exefs", "romfs", "generate", "compile", "shaders", "warm", "cleanup")
WEIGHTS = {
    "check": 1,
    "exefs": 2,
    "romfs": 30,
    "generate": 15,
    "compile": 28,
    "shaders": 18,
    "warm": 5,
    "cleanup": 1,
}  # fmt: skip (a first setup's minutes per step, roughly)
LABELS = {"check": "Checking your copy", "exefs": "Copying the game code", "romfs": "Copying the game files",
          "generate": "Generating the game code", "compile": "Compiling the game",
          "shaders": "Translating the shaders", "warm": "Warming up the graphics cache",
          "cleanup": "Cleaning up"}  # fmt: skip
TOTAL = sum(WEIGHTS[s] for s in STEPS)


def show(done_w: float, step: str) -> None:
    print(f"progress {min(max(done_w / TOTAL, 0.0), 1.0):.4f} {LABELS.get(step, step)}", flush=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--source", required=True, help="the player's copy (a folder, .nup or .nx)")
    ap.add_argument("--game", required=True, help="the game installation folder")
    ap.add_argument("--romfs", choices=("copy", "link"), default="copy")
    args = ap.parse_args()
    prep = Path(__file__).resolve().parent / "prepare.py"
    p = subprocess.Popen([sys.executable, "-I", str(prep), "--source", args.source, "--game", args.game,
                          "--romfs", args.romfs], stdout=subprocess.PIPE, text=True, encoding="utf-8",
                         errors="replace")  # fmt: skip
    assert p.stdout is not None
    done: set[str] = set()  # steps finished (a repeat "step" after a retry adds nothing twice)
    cur = ""

    def base() -> float:
        return sum(WEIGHTS[s] for s in done)

    for line in p.stdout:
        w = line.split()
        if not w:
            continue
        if w[0] == "step" and len(w) == 2 and w[1] in WEIGHTS:
            if cur and cur != w[1]:
                done.add(cur)
            cur = w[1]
            show(base(), cur)
        elif w[0] == "progress" and len(w) == 3 and cur:
            try:
                d, t = float(w[1]), float(w[2])
            except ValueError:
                continue
            show(base() + (WEIGHTS[cur] * d / t if t else 0.0), cur)
        elif w[0] == "skip" and len(w) == 2 and w[1] in WEIGHTS:
            done.add(w[1])
            cur = w[1]
            show(base(), cur)
        elif w[0] == "version":
            pass  # the copy's version (prepare.py logs it): nothing for the bar
        elif w[0] == "done":
            show(TOTAL, "cleanup")
    rc = p.wait()
    if rc:
        print("setup failed (logs/setup.log in the game folder says why)", file=sys.stderr)
    return rc


if __name__ == "__main__":
    sys.exit(main())
