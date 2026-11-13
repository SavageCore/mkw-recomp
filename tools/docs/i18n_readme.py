"""Builds docs/i18n/README.<lang>.md from a translated body and the language's strings, or checks they are current.

Why: the README is read in the languages of most of the players who report here; a translation is written by hand once
(docs/i18n/body/<lang>.md) and this keeps the parts every language repeats in step: the centred title, the download
buttons, the link row and the language bar (the same as README.md's, its own language in bold), the body's relative
links moved two folders up, and a "translated from" line with the hash of the README.md it follows. check.py runs the
check and warns, without refusing the commit, when README.md changed after a translation (Adrià 13/11/2026).

Usage: uv run tools/docs/i18n_readme.py            (writes every docs/i18n/README.<lang>.md)
       uv run tools/docs/i18n_readme.py --check    (lists the translations older than README.md; exit 0 either way)
"""

import argparse
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
I18N = ROOT / "docs/i18n"
REPO = "https://github.com/adriadam10/mkw-recomp"
# (code, name in its own language, right to left)
LANGS = [("zh-CN", "简体中文", False), ("es", "Español", False), ("ar", "العربية", True), ("hi", "हिन्दी", False),
         ("pt-BR", "Português (Brasil)", False), ("pt-PT", "Português (Portugal)", False)]  # fmt: skip
# per language: tagline, download, latest for Windows / Linux (AppImage) / Linux ARM, and the link row's names
# fmt: off
STRINGS = {
    "zh-CN": ("《马力欧赛车 世界》（Nintendo Switch 2）的重编译版本，可在 Linux 和 Windows 上原生运行。", "下载",
              "Windows 最新版", "Linux 最新版 (AppImage)", "Linux ARM 最新版",
              ["发布", "更新日志", "参与贡献", "来源", "路线图", "性能", "问题", "讨论"]),
    "es": ("Mario Kart World (Nintendo Switch 2) recompilado para funcionar de forma nativa en Linux y Windows.",
           "Descargar", "última para Windows", "última para Linux (AppImage)", "última para Linux ARM",
           ["Versiones", "Cambios", "Contribuir", "Fuentes", "Hoja de ruta", "Rendimiento", "Issues", "Discussions"]),
    "ar": ("ماريو كارت وورلد (Nintendo Switch 2) معاد تجميعها لتعمل أصليًا على Linux وWindows.", "تنزيل",
           "أحدث إصدار لـ Windows", "أحدث إصدار لـ Linux (AppImage)", "أحدث إصدار لـ Linux ARM",
           ["الإصدارات", "سجل التغييرات", "المساهمة", "المصادر", "خارطة الطريق", "الأداء", "المشكلات", "النقاشات"]),
    "hi": ("Mario Kart World (Nintendo Switch 2), Linux और Windows पर नेटिव रूप से चलने के लिए रीकम्पाइल किया गया।",
           "डाउनलोड", "Windows के लिए नवीनतम", "Linux के लिए नवीनतम (AppImage)", "Linux ARM के लिए नवीनतम",
           ["रिलीज़", "बदलाव", "योगदान", "स्रोत", "रोडमैप", "प्रदर्शन", "इश्यू", "चर्चाएँ"]),
    "pt-BR": ("Mario Kart World (Nintendo Switch 2) recompilado para rodar nativamente no Linux e no Windows.",
              "Baixar", "mais recente para Windows", "mais recente para Linux (AppImage)",
              "mais recente para Linux ARM",
              ["Versões", "Mudanças", "Contribuir", "Fontes", "Roteiro", "Desempenho", "Issues", "Discussões"]),
    "pt-PT": ("Mario Kart World (Nintendo Switch 2) recompilado para correr nativamente em Linux e Windows.",
              "Transferir", "mais recente para Windows", "mais recente para Linux (AppImage)",
              "mais recente para Linux ARM",
              ["Versões", "Alterações", "Contribuir", "Fontes", "Roteiro", "Desempenho", "Issues", "Discussões"]),
}
# fmt: on
LINKS = [
    f"{REPO}/releases",
    "../../CHANGELOG.md",
    "../../CONTRIBUTING.md",
    "../../docs/research.md#sources",
    "../../docs/roadmap.md",
    "../../docs/performance.md",
    f"{REPO}/issues",
    f"{REPO}/discussions",
]  # fmt: skip
SOURCE = re.compile(r"<!-- translated from README.md ([0-9a-f]{12}) -->")
REL_MD = re.compile(r"(\]\()(?!https?:|#|mailto:|\.\./)([^)\s]+)")
REL_HTML = re.compile(r'((?:href|src)=")(?!https?:|#|mailto:|\.\./)([^"]+)')


def readme_hash() -> str:
    return hashlib.sha256((ROOT / "README.md").read_bytes()).hexdigest()[:12]


def badge(text: str) -> str:
    return text.replace("-", "--").replace("_", "__").replace(" ", "_")


def bar(code: str) -> str:
    items = ['<a href="../../README.md">English</a>']
    for c, name, _ in LANGS:
        items.append(f"<b>{name}</b>" if c == code else f'<a href="README.{c}.md">{name}</a>')
    return '<!-- i18n-bar:start -->\n<p align="center">\n  ' + " ·\n  ".join(items) + "\n</p>\n<!-- i18n-bar:end -->"


def build(code: str, rtl: bool) -> str:
    tag, dl, win, lin, arm, names = STRINGS[code]
    from urllib.parse import quote

    def button(what: str, color: str) -> str:
        url = "https://img.shields.io/badge/" + quote(badge("⬇ " + dl)) + "-" + quote(badge(what)) + f"-{color}"
        return (f'  <a href="{REPO}/releases/latest"><img src="{url}?style=for-the-badge&labelColor=1b2740" '
                f'alt="{dl}: {what}"></a>')  # fmt: skip

    links = " ·\n  ".join(f'<a href="{u}">{n}</a>' if i else f'<b><a href="{u}">{n}</a></b>'
                          for i, (u, n) in enumerate(zip(LINKS, names)))  # fmt: skip
    body = (I18N / "body" / f"{code}.md").read_text(encoding="utf-8").strip()
    body = REL_HTML.sub(r"\1../../\2", REL_MD.sub(r"\1../../\2", body))
    if rtl:
        body = f'<div dir="rtl">\n\n{body}\n\n</div>'
    head = "\n\n".join([
        f"<!-- translated from README.md {readme_hash()} -->",
        '<p align="center"><img src="../logo.svg" width="128" alt="mkw-recomp logo"></p>',
        '<h1 align="center">mkw-recomp</h1>',
        f'<p align="center">\n  <b>{tag}</b>\n</p>',
        '<p align="center">\n' + "\n".join([button(win, "0078d6"), button(lin, "e95420"), button(arm, "8957e5")])
        + "\n</p>",
        f'<p align="center">\n  {links}\n</p>',
        bar(code),
    ])  # fmt: skip
    return head + "\n\n" + body + "\n"


def stale() -> list[str]:
    now = readme_hash()
    out = []
    for code, _, _ in LANGS:
        p = I18N / f"README.{code}.md"
        m = SOURCE.search(p.read_text(encoding="utf-8")) if p.is_file() else None
        if not m or m.group(1) != now:
            out.append(f"docs/i18n/README.{code}.md")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    if args.check:
        for p in stale():
            print(
                f"note: {p} was translated from an older README.md: update docs/i18n/body/ and run "
                "tools/docs/i18n_readme.py"
            )
        return 0
    for code, _, rtl in LANGS:
        (I18N / f"README.{code}.md").write_text(build(code, rtl), encoding="utf-8")
        print(f"docs/i18n/README.{code}.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
