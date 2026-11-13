# docs/i18n/body

The translated text of the README, one file per language (`zh-CN.md`, `es.md`, `ar.md`, `hi.md`, `pt-BR.md`,
`pt-PT.md`), from the paragraph after the title to the end, with links as from the repo's root.
`tools/docs/i18n_readme.py` adds the title, buttons, link row and language bar and writes `../README.<lang>.md`.
Button and menu names are the start screen's own in that language where it has one (`i18n/*.po`); Arabic and Hindi,
which it does not have, keep them in English, as the player sees them.
