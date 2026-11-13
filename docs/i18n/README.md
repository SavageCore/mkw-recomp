# docs/i18n

The root README in other languages, linked from the language bar at the top of every one of them. Each
`README.<lang>.md` is built by `uv run tools/docs/i18n_readme.py` from its translated body in [body/](body/README.md):
edit the body, never the built file. Its first line names the `README.md` it was translated from; `tools/docs/check.py`
notes the ones older than the current `README.md`.

| File | Language |
|---|---|
| [README.zh-CN.md](README.zh-CN.md) | Simplified Chinese |
| [README.es.md](README.es.md) | Spanish |
| [README.ar.md](README.ar.md) | Arabic (right to left) |
| [README.hi.md](README.hi.md) | Hindi |
| [README.pt-BR.md](README.pt-BR.md) | Portuguese (Brazil) |
| [README.pt-PT.md](README.pt-PT.md) | Portuguese (Portugal) |
