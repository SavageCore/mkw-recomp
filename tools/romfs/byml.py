"""Minimal BYML reader (v2-v7 common nodes, either byte order) printing the tree. Usage: byml.py FILE

Little-endian ("YB") is what most of the romfs uses; GameData/GameDataList is big-endian ("BY") and keys its entries by
hash: the v7 hash dictionaries (0x20 with 32-bit, 0x21 with 64-bit hashes) are printed with the hash in hex as key.
"""
import struct
import sys

d = open(sys.argv[1], "rb").read()
assert d[:2] in (b"YB", b"BY"), d[:4]
E = "<" if d[:2] == b"YB" else ">"
BO = "little" if E == "<" else "big"
ver, keys_off, strs_off, root_off = struct.unpack_from(E + "HIII", d, 2)


def u24(off):
    return int.from_bytes(d[off: off + 3], BO)


def u32(off):
    return struct.unpack_from(E + "I", d, off)[0]


def table(off):
    if not off:
        return []
    n = u24(off + 1)
    offs = struct.unpack_from(f"{E}{n + 1}I", d, off + 4)
    return [d[off + o: d.index(0, off + o)].decode() for o in offs[:n]]


keys, strs = table(keys_off), table(strs_off)


def val(t, v):
    if t == 0xA0: return strs[v]
    if t == 0xD0: return bool(v)
    if t == 0xD1: return struct.unpack(E + "i", struct.pack(E + "I", v))[0]
    if t == 0xD2: return struct.unpack(E + "f", struct.pack(E + "I", v))[0]
    if t == 0xD3: return v
    if t in (0xD4, 0xD5, 0xD6): return struct.unpack_from(E + ("q" if t == 0xD4 else "Q" if t == 0xD5 else "d"), d, v)[0]
    if t == 0xFF: return None
    if t in (0x20, 0x21, 0xC0, 0xC1): return node(v)
    return f"<type {t:#x} {v:#x}>"


def node(off):
    t, n = d[off], u24(off + 1)
    if t == 0xC1:
        out = {}
        for k in range(n):
            e = off + 4 + 8 * k
            ki, et, v = u24(e), d[e + 3], u32(e + 4)
            out[keys[ki]] = val(et, v)
        return out
    if t in (0x20, 0x21):
        hw = 4 if t == 0x20 else 8
        ew = hw + 4
        types = d[off + 4 + ew * n: off + 4 + ew * n + n]
        out = {}
        for k in range(n):
            e = off + 4 + ew * k
            h = int.from_bytes(d[e: e + hw], BO)
            out[f"{h:#0{2 + 2 * hw}x}"] = val(types[k], u32(e + hw))
        return out
    if t == 0xC0:
        types = d[off + 4: off + 4 + n]
        base = off + 4 + ((n + 3) & ~3)
        return [val(types[k], u32(base + 4 * k)) for k in range(n)]
    return f"<node {t:#x}>"


import json

print(f"version {ver}")
print(json.dumps(node(root_off), indent=1, default=str))
