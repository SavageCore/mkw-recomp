#!/usr/bin/env bash
# Refuse staged executables without a #! line: called by path, bash runs them, and a Python one's `import argparse`
# becomes ImageMagick's `import`, waiting for a click (13/11/2026: three hangs of 2 min on an issue tool).
# Run by tools/git/pre-commit, in the repo being committed; another repo's hook can call it too.
# Usage: tools/git/shebangs.sh
set -euo pipefail
bad=$(git diff --cached --name-only -z --diff-filter=ACM | xargs -0 -r git ls-files -s -- \
    | awk '$1 == "100755" { print $4 }' | while read -r f; do [[ $(git show ":$f" | head -c2) == '#!' ]] || echo "$f"; done)
if [[ -n $bad ]]; then
    echo "pre-commit: executable without a #! line (it would run under bash):" >&2
    echo "$bad" >&2
    exit 1
fi
