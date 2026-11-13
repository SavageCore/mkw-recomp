# Linux probes without root

| Tool | What | Usage |
|---|---|---|
| `ioctl_shim.c` | `LD_PRELOAD` over `ioctl`: calls, total and longest ms per thread and request code every 2 s (which driver calls a thread sits in) | `cc -O2 -shared -fPIC -o build/ioctl_shim.so tools/analisis/linux/ioctl_shim.c -ldl -lpthread`, then `LIVE_WRAP="env LD_PRELOAD=$PWD/build/ioctl_shim.so" tools/run/live.sh NAME` |
| `offcpu.py` | Samples each thread's state and `wchan` through `/proc`: running, futex (ours) or kernel waits, per window | `python3 -I tools/analisis/linux/offcpu.py $(pgrep -x mkw) 250 2` |
