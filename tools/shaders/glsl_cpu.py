"""Runs a straight-line translated fragment shader (build/spirv/<key>.glsl) on the CPU for one pixel, with the UBO
values a MKW_DUMP_UBO dump (passes.txt) or a MKW_TRACE_UBO trace printed for one pass: glsl_cpu.py KEY FILE PASS X Y,
PASS a dump's pass number or a trace's "<frame>-<n>". Compiles it as C (gcc). Why: a shader whose GPU output is wrong
runs here on the same inputs; the same result points at the runtime, a different one at the translation."""
import re
import struct
import subprocess
import sys
from pathlib import Path

key, passes, pnum, x, y = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), float(sys.argv[5])
label = f"pass {int(pnum):04d}:" if pnum.isdigit() else f"pass {pnum}:"
src = Path(f"build/spirv/{key}.glsl").read_text()
body = src[src.index("void main() {") + len("void main() {"):src.rindex("}")]
text = Path(passes).read_text()
blk = text[text.index(label):]
blk = blk[:blk.index("\npass ", 10)] if "\npass " in blk[10:] else blk
ubos = {}
for m in re.finditer(r"fs ubo c(\d+) addr \S+ size \S+\n((?:      .*\n)+)", blk):
    vals = {}
    for r in re.finditer(r"\[(\d+)\] (\S+) (\S+) (\S+) (\S+)", m.group(2)):
        vals[int(r.group(1))] = [float(v) for v in r.groups()[1:]]
    ubos[int(m.group(1))] = vals
outs = sorted(set(int(n) for n in re.findall(r"out_color(\d+)", body)))
c = ["#include <math.h>\n#include <stdio.h>\n#include <string.h>\n#include <stdbool.h>",
     "typedef unsigned int uint; typedef unsigned long long u64;",
     "static float uintBitsToFloat(uint u){float f;memcpy(&f,&u,4);return f;}",
     "static uint floatBitsToUint(float f){uint u;memcpy(&u,&f,4);return u;}",
     "static float nmin(float a,float b){return fminf(a,b);} static float nmax(float a,float b){return fmaxf(a,b);}",
     "static float nclamp(float x,float l,float h){return fminf(fmaxf(x,l),h);}",
     "static float clampf(double x,double l,double h){return x<l?l:x>h?h:x;}",
     "static float inversesqrt(float x){return 1.0f/sqrtf(x);}",
     "#define uint(x) ((uint)(x))\n#define int(x) ((int)(x))\n#define uint64_t(x) ((u64)(x))\n#define clamp clampf",
     "#define fma(a,b,c) fmaf(a,b,c)\n#define log2(x) log2f(x)\n#define exp2(x) exp2f(x)\n#define abs(x) fabsf(x)\n#define trunc(x) truncf(x)",
     "#define min(a,b) ((a)<(b)?(a):(b))\n#define LOOP_CAP 0x1000u",
     "typedef union { uint d[4096][4]; uint w[16384]; } CB;"]  # w: the word view cbNw (sm86 mem.cb_word)
# cbNw_at: the bounded word read (head.py); here the bound is the bank's 64 KiB, the dump keeps no bound range
for b in sorted(ubos): c.append(f"static CB cb{b};\nstatic uint cb{b}w_at(uint i){{return i<16384u?cb{b}.w[i]:0u;}}")
c.append("static float gl_FragCoord[4];")
for o in outs: c.append(f"static float out_color{o}[4];")
c.append("static void shader(void) {" + body + "}")
c.append("int main(void) {")
for b, vals in ubos.items():
    for i, v in vals.items():
        for j in range(4): c.append(f"cb{b}.d[{i}][{j}]={struct.unpack('<I', struct.pack('<f', v[j]))[0]:#x}u;")
c.append(f"gl_FragCoord[0]={x}f; gl_FragCoord[1]={y}f; gl_FragCoord[2]=0; gl_FragCoord[3]=1; shader();")
for o in outs: c.append(f'printf("out{o} %g %g %g %g\\n",out_color{o}[0],out_color{o}[1],out_color{o}[2],out_color{o}[3]);')
c.append("return 0; }")
d = Path("scratchpad/tmp/glslcpu"); d.mkdir(parents=True, exist_ok=True)
(d / "s.c").write_text("\n".join(c).replace("0x7fffffffu", "0x7fffffffu"))
subprocess.run(["gcc", "-O0", "-w", "-o", str(d / "s"), str(d / "s.c"), "-lm"], check=True)
print(subprocess.run([str(d / "s")], capture_output=True, text=True, check=True).stdout, end="")
