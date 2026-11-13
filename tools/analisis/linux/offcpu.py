#!/usr/bin/env python3
"""Samples every thread of PID through /proc (state, wchan, syscall) at RATE Hz and prints, every WIN seconds, per
thread name, the share of samples running (R) and sleeping (S/D) by wchan: where a thread's wall time goes without
root (perf sched needs it). Why: the replay thread spent 208 ms of wall a frame with 61 of CPU in the loading screen;
this told futex (waiting for the game) from dma_fence (the kernel's sparse binds) (carga-wc1, 14/11/2026).
Usage: python3 -I tools/analisis/linux/offcpu.py PID [RATE=250] [WIN=2] [NAMES_REGEX]   (defaults to the gfx threads)"""
import os, re, sys, time, collections
pid = int(sys.argv[1]); rate = float(sys.argv[2]) if len(sys.argv) > 2 else 250; win = float(sys.argv[3]) if len(sys.argv) > 3 else 2
want = re.compile(sys.argv[4]) if len(sys.argv) > 4 else re.compile('vk replay|vk recorder|vk submit|guest main|vk shadow')
def rd(p):
    try:
        with open(p) as f: return f.read()
    except OSError: return ''
tids = {}
def scan():
    for t in os.listdir(f'/proc/{pid}/task'):
        if t in tids: continue
        n = rd(f'/proc/{pid}/task/{t}/comm').strip()
        if want.search(n): tids[t] = n
scan(); t0 = time.monotonic(); nxt = t0 + win; last_scan = t0
hist = collections.defaultdict(collections.Counter)
while True:
    now = time.monotonic()
    if now - last_scan > 1: scan(); last_scan = now
    for t, n in list(tids.items()):
        st = rd(f'/proc/{pid}/task/{t}/stat')
        if not st:
            if not os.path.exists(f'/proc/{pid}'): sys.exit(0)
            continue
        state = st[st.rindex(')') + 2]
        if state == 'R':
            key = 'R'
        else:
            wc = rd(f'/proc/{pid}/task/{t}/wchan').strip() or '?'
            sc = rd(f'/proc/{pid}/task/{t}/syscall').split()
            nr = sc[0] if sc else '?'
            arg = ''
            if nr == '202': arg = ' futex ' + (sc[1] if len(sc) > 1 else '')      # x86-64 futex: uaddr
            elif nr == '16': arg = ' ioctl ' + (sc[2] if len(sc) > 2 else '')     # request
            key = f'{state} {wc} sys{nr}{arg}'
        hist[n][key] += 1
    if now >= nxt:
        print(f'offcpu: t={now:.1f} ({win:.0f} s)', flush=True)
        for n, c in sorted(hist.items()):
            tot = sum(c.values()) or 1
            top = ', '.join(f'{k} {v * 100 // tot}%' for k, v in c.most_common(6))
            print(f'offcpu:   {n:12s} {tot:5d}: {top}', flush=True)
        hist.clear(); nxt = now + win
    d = 1 / rate - (time.monotonic() - now)
    if d > 0: time.sleep(d)
