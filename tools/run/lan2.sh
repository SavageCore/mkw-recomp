#!/usr/bin/env bash
# Two game instances on one PC, each with its own IPv4 address, to test LAN play (MKW_LAN=1) without a second machine.
# Why: side by side in the host's network both instances report the same address and bind the same Pia port, and
# SO_REUSEPORT (which Pia sets) then splits unicast datagrams between them, so neither sees the other whole. Here a
# user namespace (no root) holds two network namespaces joined by a veth pair: A at 10.77.0.1, B at 10.77.0.2 (/24,
# broadcast 10.77.0.255), each told its address with MKW_LAN_IP. Both run in one job unit, so JOB_MEM caps the pair
# (default 14G: 7G each, live.sh's cap). Each has its log, control FIFO and profile: build/run/NAME-a.{log,ctl},
# NAME-b.{log,ctl}; B's profile is build/profile-b, copied from build/profile the first time (same account, so the
# same save; the pipeline cache is not shared between two writers).
# Usage: tools/run/lan2.sh NAME [SECONDS]   (default 900; then both are killed). Extra env (MKW_*) passes through.
# LAN2_PREFIX=32 gives both addresses a /32 mask, as a VPN such as Tailscale does (the game refuses it: #148).
# Presses: tools/run/ctl.sh NAME-a press 5 A, ctl.sh NAME-b ...; stop both: tools/run/ctl.sh NAME stop.
set -euo pipefail
cd "$(dirname "$0")/../.."

if [ "${1:-}" = --inner ]; then   # in the user namespace and network namespace A (job unit below)
  n=$2 secs=$3 r=build/run/$2 p=${LAN2_PREFIX:-24}
  ip link set lo up
  # B: its own network namespace; it says when it is inside, then waits for its link before starting the game
  unshare -n bash -c 'ip link set lo up; touch "$1-b.ns"; while [ ! -e "$1-b.go" ]; do sleep 0.1; done
    exec env MKW_CTL="$PWD/$1-b.ctl" MKW_LAN_IP=10.77.0.2 MKW_PROFILE_DIR="$PWD/build/profile-b" \
      timeout -s KILL "$2" build/mkw > "$1-b.log" 2>&1' _ "$r" "$secs" &
  b=$!
  while [ ! -e "$r-b.ns" ]; do sleep 0.05; done
  ip link add vA type veth peer name vB netns "$b"
  ip addr add 10.77.0.1/$p dev vA
  ip link set vA up
  nsenter -t "$b" -n --preserve-credentials sh -c 'ip addr add 10.77.0.2/'"$p"' dev vB && ip link set vB up'
  touch "$r-b.go"
  MKW_CTL=$PWD/$r-a.ctl MKW_LAN_IP=10.77.0.1 timeout -s KILL "$secs" build/mkw > "$r-a.log" 2>&1 &
  wait
  exit 0
fi

n=${1:?usage: lan2.sh NAME [SECONDS]} secs=${2:-900}
. tools/run/why.sh "$n"
if [ -n "$(find runtime -newer build/mkw \( -name '*.c' -o -name '*.h' -o -name '*.cpp' \) -print -quit)" ]; then
  echo "lan2: runtime/ is newer than build/mkw: build first (tools/build/build.py)" >&2; exit 1
fi
if pgrep -x mkw >/dev/null; then echo "lan2: a build/mkw is already running" >&2; exit 1; fi
tools/run/gpu_ok.sh || exit 1
# two runs: twice live.sh's floor of 8500 MB
need=${MIN_AVAIL:-17000}
avail=$(tools/run/avail.sh --wait "$need")
[ "$avail" -ge "$need" ] || { echo "lan2: only $avail MB available: not starting"; exit 1; }
mkdir -p build/run
[ -d build/profile-b ] || cp -a build/profile build/profile-b
rm -f "build/run/$n-b.ns" "build/run/$n-b.go"
for i in a b; do rm -f "build/run/$n-$i.ctl"; mkfifo "build/run/$n-$i.ctl"; done
# live.sh's settings
export MKW_LAN=${MKW_LAN:-1} MKW_GPU_GUARD=${MKW_GPU_GUARD:-1} MKW_ROMFS_PATCHES=${MKW_ROMFS_PATCHES-build/romfs-res}
export MKW_RESOLUTION=${MKW_RESOLUTION:-720} MKW_SPARSE_MIB=${MKW_SPARSE_MIB:-768} MKW_GPU_MEM_LIMIT=${MKW_GPU_MEM_LIMIT:-4096}
export MKW_TRACE=${MKW_TRACE:-gfx,present} MKW_SEQ_WATCH=${MKW_SEQ_WATCH:-1} MKW_INPUT=${MKW_INPUT:-none}
export MKW_FULLSCREEN=${MKW_FULLSCREEN:-0} MKW_WINDOW_SIZE=${MKW_WINDOW_SIZE:-1280x720}
export MKW_BACKGROUND_WINDOW=${MKW_BACKGROUND_WINDOW:-1} MKW_ASSUME_FOCUS=${MKW_ASSUME_FOCUS:-1}
tools/run/kwin-rule.sh --ensure >/dev/null
mapfile -t vars < <(env | grep -E '^MKW_' | grep -v '^MKW_RUN_WHY=')
JOB_MEM=${JOB_MEM:-14G} tools/run/job.sh start "$n" env "${vars[@]}" LAN2_PREFIX="${LAN2_PREFIX:-24}" unshare -Ur -n bash tools/run/lan2.sh --inner "$n" "$secs"
echo "lan2: $n up: A 10.77.0.1 (ctl.sh $n-a ...), B 10.77.0.2 (ctl.sh $n-b ...); stop: ctl.sh $n stop"
