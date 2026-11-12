"""Code patch table for the generator: guest instructions replaced in the lifted C, switchable at run time.

Lifted code cannot be byte-patched when the game runs, so a patch is applied when the C is generated. A patch file
(gen.py --patches) has two kinds of lines; `#` starts a comment, fields are separated by whitespace:

  set  <name> <env> <default>               a patch set: a run-time switch <env>=0/1, <default> when it is unset
  site <set> <module> <address> <original> <replacement> [<replacement> ...]

  spin <module> <head> <back-edge> [<back-edge> ...]

<address> is a module offset (hex), <original> the instruction word the module has there (checked; a mismatch stops
the generator), each <replacement> an instruction word (hex) or `@page:x<n>`, which is `adrp x<n>, <patch page>`
encoded for the site's address. The patch page is the first 4 KiB page after main's image (layout.py leaves the rest
of the 2 MiB-aligned module slot free); the runtime commits it and owns its contents.

The replacement words are lifted at the site's address in place of the original instruction, and the generated C reads
`if (rt_patch_on[<set>]) { replacement } else { original }`: one byte load and a branch per executed site. Replacement
words must not branch except a final `bl` (lifted with the site's return address); the original must not be a branch
other than `bl`. So the function's control flow, labels and jump tables are the original's. The generator writes
patches.h (set names, switches, the page address) next to layout.h for the runtime.

A `spin` line names a spin wait found by tools/spin_waits.py: a loop with head <head> that only another thread can end,
and the direct branches back to it (module offsets). Each back-edge (checked: a b, b.cond, cbz / cbnz or tbz / tbnz to
<head>) is lifted as `rt_spin_gate(c, <head>); goto <head>`, which keeps every register and only adds time
(runtime/kernel.c kern_spin_gate).
"""
from pathlib import Path

PAGE = 0x1000


class PatchError(ValueError):
    pass


def adrp(pc, target, rd):
    imm = (target >> 12) - (pc >> 12)
    if not -(1 << 20) <= imm < (1 << 20):
        raise PatchError(f'{pc:#x}: patch page {target:#x} out of ADRP range')
    imm &= 0x1FFFFF
    return 0x90000000 | (imm & 3) << 29 | (imm >> 2) << 5 | rd


class Table:
    """sets: [(name, env, default)]; sites: {module: {offset: (set index, [word | ('page', reg)], original)}};
    spans: {module: {offset: (set index, [words])}} (filled by a later span mechanism, if any); page."""

    def __init__(self, sets=(), sites=None, page=0, spins=None, spans=None):
        self.sets = list(sets)
        self.sites = sites or {}
        self.page = page
        self.spins = spins or {}   # {module: {back-edge offset: head offset}}
        self.spans = spans or {}   # {module: {offset: (set index, [words])}}
        self.sources = {}          # {(module, offset): mod name or "table"}: who patched the site

    def spins_for(self, name, base):
        """{absolute back-edge address: absolute head address}."""
        return {base + b: base + h for b, h in self.spins.get(name, {}).items()}

    def for_module(self, name, base):
        """{absolute address: (set index, [words])} with @page resolved."""
        out = {}
        for off, (k, repl, _) in self.sites.get(name, {}).items():
            a = base + off
            out[a] = (k, [adrp(a, self.page, w[1]) if isinstance(w, tuple) else w for w in repl])
        return out


def load(path, mods):
    """Table from a patch file; mods is layout() output [(name, base, size)]."""
    sets, index, sites, spins, spans = [], {}, {}, {}, {}
    bases = {n: (b, s) for n, b, s in mods}
    for no, raw in enumerate(Path(path).read_text().splitlines(), 1):
        f = raw.split('#', 1)[0].split()
        if not f:
            continue
        where = f'{path}:{no}'
        if f[0] == 'set' and len(f) == 4:
            if f[1] in index:
                raise PatchError(f'{where}: set {f[1]} defined twice')
            index[f[1]] = len(sets)
            sets.append((f[1], f[2], int(f[3])))
        elif f[0] == 'site' and len(f) >= 6:
            if f[1] not in index:
                raise PatchError(f'{where}: unknown set {f[1]}')
            if f[2] not in bases:
                raise PatchError(f'{where}: module {f[2]} not in the layout')
            off, orig = int(f[3], 16), int(f[4], 16)
            repl = []
            for w in f[5:]:
                if w.startswith('@page:x'):
                    repl.append(('page', int(w[7:])))
                else:
                    repl.append(int(w, 16))
            mod = sites.setdefault(f[2], {})
            if off in mod:
                raise PatchError(f'{where}: {f[2]}+{off:#x} patched twice')
            mod[off] = (index[f[1]], repl, orig)
        elif f[0] == 'spin' and len(f) >= 4:
            if f[1] not in bases:
                raise PatchError(f'{where}: module {f[1]} not in the layout')
            mod, head = spins.setdefault(f[1], {}), int(f[2], 16)
            for b in f[3:]:
                b = int(b, 16)
                if b in mod:
                    raise PatchError(f'{where}: {f[1]}+{b:#x} is a back-edge twice')
                mod[b] = head
        else:
            raise PatchError(f'{where}: cannot parse {raw!r}')
    base, size = bases['main'][0], bases['main'][1]
    page = (base + size + PAGE - 1) & ~(PAGE - 1)
    nxt = min((b for _, b, _ in mods if b > base), default=None)
    if nxt is not None and page + PAGE > nxt:
        raise PatchError(f'no room for the patch page after main ({page:#x})')
    table = Table(sets, sites, page, spins, spans)
    for name, mod in sites.items():   # who patched what, for the mod merger's overlap report
        for off in mod:
            table.sources[(name, off)] = 'table'
    return table


def load_mods(table, mods_dir, bases):
    """Merges code patches shipped by mods into the table: each <mod>/mod.json "patches" lists the mod's own
    spans (a branch over code, which a site cannot replace) and sites (single instructions), in the same shape
    as the patch file but JSON. The sets land in the generated patches.h like the table's own (default off);
    enabling the mod sets their switches (the Mods page reads the same mod.json)."""
    import json
    import os
    try:
        names = sorted(os.listdir(mods_dir))
    except OSError:
        return   # no mods installed: the vanilla table
    for name in names:
        path = os.path.join(mods_dir, name, "mod.json")
        try:
            with open(path, encoding="utf-8") as f:
                mp = json.load(f).get("patches")
        except (OSError, ValueError):
            continue
        if not mp:
            continue
        if mp.get("spans"):
            raise PatchError(f'{where}: spans need the span mechanism (a later commit)')
        where = f'{path}: patches'
        index = {s[0]: i for i, s in enumerate(table.sets)}

        def clash(module, off, nwords):
            # names who already patches [off, off + 4*nwords): a site, a span, or None (like the mod
            # merger's overlap report: two mods patching one address stop the generate, never merge silently)
            if off in table.sites.get(module, {}):
                return table.sources.get((module, off))
            for o, (k, ws) in table.spans.get(module, {}).items():
                if off < o + 4 * len(ws) and o < off + 4 * nwords:
                    return table.sources.get((module, o))
            return None

        for st in mp.get("sites", []):
            for k in ("set", "env", "module", "off", "orig", "repl"):
                if k not in st:
                    raise PatchError(f'{where}: site misses "{k}"')
            if not st["env"].startswith("MKW_"):
                raise PatchError(f'{where}: env switch {st["env"]} needs its MKW_ prefix')
            if st["set"] in index:
                if table.sets[index[st["set"]]][1] != st["env"]:
                    raise PatchError(f'{where}: set {st["set"]} has two env switches')
            else:
                index[st["set"]] = len(table.sets)
                table.sets.append((st["set"], st["env"], 0))
            if st["module"] not in bases:
                raise PatchError(f'{where}: module {st["module"]} not in the layout')
            try:
                off, orig = int(st["off"], 16), int(st["orig"], 16)
                repl = []
                for w in st["repl"]:
                    repl.append(("page", int(w[7:], 16)) if isinstance(w, str) and w.startswith("@page:x") else int(w, 16))
            except ValueError:
                raise PatchError(f'{where}: site {st["set"]} has non-hex words')
            other = clash(st["module"], off, 1)
            if other is not None and other != name:
                raise PatchError(f'{where}: site at {st["module"]}+{off:#x} overlaps {other}, not merged')
            mod = table.sites.setdefault(st["module"], {})
            if off in mod:
                raise PatchError(f'{where}: {st["module"]}+{off:#x} patched twice')
            for o, (k, ws) in table.spans.get(st["module"], {}).items():
                if o <= off < o + 4 * len(ws):
                    raise PatchError(f'{where}: {st["module"]}+{off:#x} is both a site and in a span')
            mod[off] = (index[st["set"]], repl, orig)
            table.sources[(st["module"], off)] = name


def check(table, mod, name):
    """Stops on a site whose original word differs from the module's."""
    for off, (_, _, want) in table.sites.get(name, {}).items():
        a = mod.base + off
        if not mod.in_text(a):
            raise PatchError(f'{name}+{off:#x}: patch site outside .text')
        have = mod.word(a)
        if have != want:
            raise PatchError(f'{name}+{off:#x}: module has {have:08x}, the patch expects {want:08x}')
    for b, head in table.spins.get(name, {}).items():
        a = mod.base + b
        if not mod.in_text(a) or branch_target(mod.word(a), a) != mod.base + head:
            raise PatchError(f'{name}+{b:#x}: not a direct branch to the spin wait head {name}+{head:#x}')


def branch_target(w, a):
    """Target of the direct branch w at a (b, b.cond, cbz / cbnz, tbz / tbnz), or None."""
    if w >> 26 == 0x05:
        return a + ((((w & 0x3FFFFFF) ^ 0x2000000) - 0x2000000) << 2)
    if w & 0xFF000010 == 0x54000000 or w & 0x7E000000 == 0x34000000:
        return a + ((((w >> 5) & 0x7FFFF) ^ 0x40000) - 0x40000 << 2)
    if w & 0x7E000000 == 0x36000000:
        return a + ((((w >> 5) & 0x3FFF) ^ 0x2000) - 0x2000 << 2)
    return None


def header_text(table, marker):
    lines = [f'// {marker}: code patch sets, generated by recomp/gen.py --patches. Do not edit or commit.',
             '#pragma once', '', f'#define RT_PATCH_COUNT {len(table.sets)}',
             f'#define RT_PATCH_PAGE {table.page:#x}ull',
             '// X(index, name, env switch, default)',
             '#define RT_PATCH_SETS(X) ' + ' '.join(f'X({i}, {n}, "{e}", {d})' for i, (n, e, d) in
                                                    enumerate(table.sets)), '']
    return '\n'.join(lines)
