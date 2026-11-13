# /// script
# requires-python = ">=3.12"
# dependencies = ["internetarchive==5.12.1"]
# ///
"""Copies the project's GitHub presence to the Internet Archive: the git history and every issue, pull request,
discussion and release as JSON in a dated item, each release's files in an item of their own, and the pages changed
since the last run to the Wayback Machine.

Why: on 13/11/2026 Hookshot, another Switch 2 recomp, vanished from GitHub with its author's account, issues and
releases included; only what others had saved on archive.org was left. A takedown or a suspended account here would
take the issues, discussions and releases with it.

Runs from .github/workflows/archive.yml once a day, with IA_ACCESS_KEY_ID and IA_SECRET_ACCESS_KEY (archive.org's
S3 keys, archive.org/account/s3.php) and GH_TOKEN. By hand it prints what it would do unless --apply.

Usage: uv run tools/github/archive.py snapshot BUNDLE [--apply]   (BUNDLE: `git bundle create repo.bundle --all`)
       uv run tools/github/archive.py releases [--apply]
       uv run tools/github/archive.py wayback [--hours 26] [--max 120] [--apply]
"""

import argparse
import gzip
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request
from datetime import UTC, datetime, timedelta
from pathlib import Path

import internetarchive as ia

REPO = os.environ.get("GITHUB_REPOSITORY", "adriadam10/mkw-recomp")
SLUG = REPO.replace("/", "-")
URL = f"https://github.com/{REPO}"
# The Wayback Machine takes about six captures a minute from one account (SPN2); one every 12 s stays under it
WAYBACK_GAP_S = 12

DISCUSSIONS = """
query($owner: String!, $name: String!, $after: String) {
  repository(owner: $owner, name: $name) {
    discussions(first: 20, after: $after) {
      pageInfo { hasNextPage endCursor }
      nodes {
        number title url createdAt updatedAt closed body author { login } category { name }
        comments(first: 100) {
          nodes {
            author { login } createdAt body
            replies(first: 100) { nodes { author { login } createdAt body } }
          }
        }
      }
    }
  }
}"""


def api(path: str) -> list | dict:
    out = subprocess.run(
        ["gh", "api", "--paginate", "--slurp", f"repos/{REPO}/{path}"], capture_output=True, text=True, check=True
    ).stdout
    pages = json.loads(out)
    return [x for p in pages for x in p] if pages and isinstance(pages[0], list) else pages[0]


def discussions() -> list[dict]:
    owner, name = REPO.split("/")
    found, after = [], None
    while True:
        args = ["gh", "api", "graphql", "-f", f"query={DISCUSSIONS}", "-f", f"owner={owner}", "-f", f"name={name}"]
        if after:
            args += ["-f", f"after={after}"]
        page = json.loads(subprocess.run(args, capture_output=True, text=True, check=True).stdout)
        d = page["data"]["repository"]["discussions"]
        found += d["nodes"]
        if not d["pageInfo"]["hasNextPage"]:
            return found
        after = d["pageInfo"]["endCursor"]


def export() -> dict:
    """Everything GitHub shows of the project but git: issues and pull requests with their comments and reviews,
    discussions with their replies, releases with their files' names."""
    pulls = api("pulls?state=all&per_page=100")
    for p in pulls:
        p["reviews"] = api(f"pulls/{p['number']}/reviews?per_page=100")
    return {
        "repo": REPO,
        "exported": datetime.now(UTC).isoformat(timespec="seconds"),
        "issues": api("issues?state=all&per_page=100"),
        "issue_comments": api("issues/comments?per_page=100"),
        "pulls": pulls,
        "pull_comments": api("pulls/comments?per_page=100"),
        "discussions": discussions(),
        "releases": api("releases?per_page=100"),
        "labels": api("labels?per_page=100"),
    }


def metadata(title: str, description: str) -> dict:
    return {
        "mediatype": "software",
        "collection": "open_source_software",
        "title": title,
        "description": description,
        "source": URL,
        "subject": ["github", "recompilation", "mkw-recomp"],
    }


def upload(identifier: str, files: dict[str, str], meta: dict, apply: bool) -> None:
    print(f"{identifier}: {', '.join(files)}")
    if not apply:
        return
    for r in ia.upload(identifier, files=files, metadata=meta, retries=5, retries_sleep=30, verbose=False):
        r.raise_for_status()


def snapshot(bundle: Path, apply: bool) -> None:
    """A dated item, as iagitup names them (github.com-OWNER-REPO_-_DATE), so the copies of each day stay apart."""
    day = datetime.now(UTC).strftime("%Y-%m-%d")
    with tempfile.TemporaryDirectory() as tmp:
        dump = Path(tmp) / f"{SLUG}-github.json.gz"
        with gzip.open(dump, "wt", encoding="utf-8") as f:
            json.dump(export(), f)
        files = {bundle.name: str(bundle), dump.name: str(dump)}
        meta = metadata(
            f"{REPO} (GitHub) {day}",
            f"The git history of <a href='{URL}'>{URL}</a> (<code>git clone {bundle.name}</code>) and every issue, "
            "pull request, discussion and release on GitHub as JSON, copied the same day.",
        )
        upload(f"github.com-{SLUG}_-_{day}", files, meta, apply)


def releases(apply: bool) -> None:
    """Each published release's files, once: an item per tag, skipped when it is already there."""
    for rel in api("releases?per_page=100"):
        if rel["draft"] or not rel["assets"]:
            continue
        identifier = f"{SLUG}-{rel['tag_name']}"
        if ia.get_item(identifier).exists:
            continue
        with tempfile.TemporaryDirectory() as tmp:
            if apply:
                subprocess.run(["gh", "release", "download", rel["tag_name"], "-R", REPO, "-D", tmp], check=True)
            files = {a["name"]: str(Path(tmp) / a["name"]) for a in rel["assets"]}
            notes = (rel["body"] or "").replace("\n", "<br>")
            meta = metadata(
                f"{REPO} {rel['tag_name']}", f"Release <a href='{rel['html_url']}'>{rel['tag_name']}</a>.<br>{notes}"
            )
            meta["date"] = rel["published_at"][:10]
            upload(identifier, files, meta, apply)


def changed_pages(hours: float, most: int) -> list[str]:
    since = datetime.now(UTC) - timedelta(hours=hours)
    stamp = since.strftime("%Y-%m-%dT%H:%M:%SZ")
    pages = [URL, f"{URL}/issues", f"{URL}/pulls", f"{URL}/discussions", f"{URL}/releases"]
    pages += [i["html_url"] for i in api(f"issues?state=all&since={stamp}&per_page=100")]
    pages += [d["url"] for d in discussions() if d["updatedAt"] >= stamp]
    return pages[:most]


def wayback(hours: float, most: int, apply: bool) -> None:
    """Save Page Now for the pages changed in the last hours; a page it refuses is reported, the rest go on."""
    auth = f"LOW {os.environ.get('IA_ACCESS_KEY_ID', '')}:{os.environ.get('IA_SECRET_ACCESS_KEY', '')}"
    failed = 0
    for i, page in enumerate(changed_pages(hours, most)):
        print(page)
        if not apply:
            continue
        if i:
            time.sleep(WAYBACK_GAP_S)
        req = urllib.request.Request(
            "https://web.archive.org/save",
            data=urllib.parse.urlencode({"url": page, "skip_first_archive": "1"}).encode(),
            headers={"Authorization": auth, "Accept": "application/json"},
        )
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                reply = json.load(r)
            if "job_id" not in reply:
                raise RuntimeError(reply.get("message", reply))
        except Exception as e:  # noqa: BLE001 - one refused page must not stop the others
            failed += 1
            print(f"  not saved: {e}", file=sys.stderr)
    if failed:
        sys.exit(f"{failed} pages not saved")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("snapshot")
    s.add_argument("bundle", type=Path)
    sub.add_parser("releases")
    w = sub.add_parser("wayback")
    w.add_argument("--hours", type=float, default=26, help="pages changed in the last HOURS (the first run: 100000)")
    w.add_argument("--max", type=int, default=120, help="at most MAX pages (one every 12 s)")
    for sp in sub.choices.values():
        sp.add_argument("--apply", action="store_true")
    a = p.parse_args()
    if a.cmd == "snapshot":
        snapshot(a.bundle, a.apply)
    elif a.cmd == "releases":
        releases(a.apply)
    else:
        wayback(a.hours, a.max, a.apply)


if __name__ == "__main__":
    main()
