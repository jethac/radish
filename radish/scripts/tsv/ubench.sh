#!/usr/bin/env bash
# Build and run the compute microbenchmarks on the tablet.
# usage: ubench.sh LABEL DRIVER_SHA12 [VAR=VALUE ...]
set -euo pipefail
. "$(dirname "$0")/common.sh"
TOP=$(cd "$(dirname "$0")/../.." && pwd)
LABEL=$1 SHA=$2; shift 2
OUT=$SRC/ubench
SYSROOT=/home/jethac/Projects/lenovo-8/streams/gpu-mesa16/work/sysroot
python3 "$TOP/tests/ubench/gen.py" "$OUT" "$PREFIX/ubench" >/dev/null
clang --target=aarch64-linux-gnu --sysroot=$SYSROOT --gcc-install-dir=$(realpath $SYSROOT/usr/lib/gcc/aarch64-linux-gnu/16) \
    -mcpu=cortex-a53 -O2 -Wall -Werror -fuse-ld=lld --rtlib=libgcc -L$SYSROOT/usr/lib \
    -I"$SRC/mesa/include" "$TOP/tests/ubench/ubench.c" -ldl -lm -o "$OUT/ubench"
ENVS=(); for e in "$@"; do ENVS+=(--setenv="$e"); done
RUNS=$SRC/runs/$LABEL; mkdir -p "$RUNS"
flock -w 1800 "$LOCK" bash -c '
    . "'"$(dirname "$0")"'/common.sh"
    adbsh "rm -rf '"$PREFIX"'/ubench; mkdir -p '"$PREFIX"'/ubench"
    adb -s $SERIAL push -q '"$OUT"'/ubench '"$OUT"'/jobs.txt '"$OUT"'/*.spv '"$PREFIX"'/ubench/ >/dev/null
    before=$(gpu_fault_count)
    adbsh "(while sleep 1; do cat /sys/class/devfreq/1c00000.gpu/cur_freq; done) > '"$PREFIX"'/ubench/freq.txt & f=\$!;
      systemd-run --unit=radish-ubench-'"$LABEL"' --wait --pipe --collect -p MemoryMax=400M -p RuntimeMaxSec=600 -p LimitCORE=0 \
      --setenv=LD_LIBRARY_PATH='"$PREFIX"'/lib --setenv=VK_DRIVER_FILES='"$PREFIX"'/lib/icd-'"$SHA"'.json '"${ENVS[*]}"' \
      '"$PREFIX"'/ubench/ubench '"$PREFIX"'/ubench/jobs.txt; kill \$f" > "'"$RUNS"'/ubench.txt" 2>&1 || true
    adbsh "cat '"$PREFIX"'/ubench/freq.txt" | sort | uniq -c > "'"$RUNS"'/freq.txt"
    echo "gpu_fault_lines before=$before after=$(gpu_fault_count)" | tee "'"$RUNS"'/faults.txt"
'
cat "$RUNS/ubench.txt"; cat "$RUNS/freq.txt"
