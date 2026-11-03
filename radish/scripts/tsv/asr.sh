#!/usr/bin/env bash
# Run straylight-listen's recogniser (--asr-input, real-time feeding) over
# command clips on the tablet, beside the live services.
# usage: asr.sh LABEL BACKEND BIN SET [--ids REGEX] [--sha DRIVER_SHA12] [VAR=VALUE ...]
#   BACKEND cpu|vulkan; BIN a name in $PREFIX/bin or an absolute device path;
#   SET en|ja|zh (lenovo-8 streams/asr/work/testset/cmd-SET).
# Output: $SRC/runs/LABEL/{log.txt,summary.txt}; end of speech to text = the
# lag of each clip's last phrase.
set -euo pipefail
. "$(dirname "$0")/common.sh"
LABEL=$1 BACKEND=$2 BIN=$3 SET=$4; shift 4
IDS=. SHA= ENVS=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --ids) IDS=$2; shift ;;
        --sha) SHA=$2; shift ;;
        *=*) ENVS+=(--setenv="$1") ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac; shift
done
[[ $BIN == /* ]] || BIN=$PREFIX/bin/$BIN
REFS=/home/jethac/Projects/lenovo-8/streams/asr/work/testset/cmd-$SET/refs.tsv
OUT=$SRC/runs/$LABEL; mkdir -p "$OUT"
W=$PREFIX/asr/wav/$SET
ARGS=()
while IFS=$'\t' read -r id wav _; do
    [[ $id =~ $IDS ]] || continue
    ARGS+=(--asr-input "$W/$id.wav")
done < "$REFS"
VK=()
if [[ $BACKEND == vulkan ]]; then
    VK=(--asr-backend vulkan --setenv=LD_LIBRARY_PATH=$PREFIX/lib --setenv=VK_DRIVER_FILES=$PREFIX/lib/icd-$SHA.json
        --setenv=TRANSCRIBE_VULKAN_KHR_PROFILE=1 --setenv=TRANSCRIBE_NO_FLASH=1 --setenv=GGML_VK_VISIBLE_DEVICES=0
        --setenv=GGML_VK_PIPELINE_CACHE=$PREFIX/asr/cache/asr-$SHA.bin
        --setenv=RADISH_BATCH_CHUNKS=8 --setenv=RADISH_TILED_BATCH_CHUNKS=8 --setenv=RADISH_SHARED_GROUPS=8)
fi
SETENV=() BACKARGS=()
for v in "${VK[@]}" "${ENVS[@]}"; do [[ $v == --setenv=* ]] && SETENV+=("$v") || BACKARGS+=("$v"); done
flock -w 1800 "$LOCK" bash -c '
    . "'"$(dirname "$0")"'/common.sh"
    adbsh "mkdir -p '"$W"' '"$PREFIX"'/asr/cache && chmod 700 '"$PREFIX"'/asr/cache"
    if [[ $(adbsh "ls '"$W"' | wc -l") -lt $(wc -l < '"$REFS"') ]]; then
        cut -f2 '"$REFS"' | xargs -I{} adb -s $SERIAL push -q {} '"$W"'/ >/dev/null
    fi
    before=$(gpu_fault_count)
    mf() { adbsh "awk \"{print \\\$12}\" /proc/\$(pidof straylight-listen)/stat; grep -E \"^(MemAvailable|SwapFree)\" /proc/meminfo | tr -s \" \" | paste -sd\" \""; }
    echo "pre listen-majflt+mem: $(mf | paste -sd" ")" > "'"$OUT"'/mem.txt"
    adbsh "(while sleep 5; do p=\$(systemctl show -p MainPID --value radish-asr-'"$LABEL"' 2>/dev/null); [ -n \"\$p\" ] && [ \"\$p\" != 0 ] && echo \$(date +%s) \$(grep -E \"VmRSS|RssAnon|RssFile|RssShmem|VmSwap\" /proc/\$p/status 2>/dev/null | tr -s \" \" | paste -sd\" \") \$(cat /sys/class/devfreq/1c00000.gpu/cur_freq); done) > '"$PREFIX"'/asr/memlog.txt 2>&1 & echo \$! > '"$PREFIX"'/asr/memlog.pid"
    adbsh "systemd-run --unit=radish-asr-'"$LABEL"' --wait --pipe --collect -p MemoryMax=850M -p MemorySwapMax=128M \
        -p Nice=-10 -p RuntimeMaxSec=900 -p LimitCORE=0 '"${SETENV[*]}"' '"$BIN"' --realtime '"${BACKARGS[*]}"' '"${ARGS[*]}"'" \
        > "'"$OUT"'/log.txt" 2>&1 || echo "exit $?" >> "'"$OUT"'/log.txt"
    adbsh "kill \$(cat '"$PREFIX"'/asr/memlog.pid); cat '"$PREFIX"'/asr/memlog.txt" > "'"$OUT"'/memlog.txt"
    echo "post listen-majflt+mem: $(mf | paste -sd" ")" >> "'"$OUT"'/mem.txt"
    echo "gpu_fault_lines before=$before after=$(gpu_fault_count)" | tee "'"$OUT"'/faults.txt"
'
python3 - "$OUT/log.txt" "$REFS" <<'PY' | tee "$OUT/summary.txt"
import re, sys, statistics
refs = {l.split('\t')[0]: l.rstrip('\n').split('\t')[2] for l in open(sys.argv[2])}
last = {}
for l in open(sys.argv[1], errors='replace'):
    m = re.search(r'straylight-listen: \S*/([^/]+)\.wav: \[([^\]]*)\] (.*) \(lag (-?\d+) ms\)', l)
    if m: last.setdefault(m[1], []).append((m[3], int(m[4])))
lags = []
for k, ph in last.items():
    lags.append(ph[-1][1]); print(f'{k}\t{ph[-1][1]} ms\t{" ".join(p[0] for p in ph)}\t| {refs.get(k, "")}')
if lags: print(f'clips {len(lags)} end-of-speech-to-text median {statistics.median(lags)} ms mean {statistics.mean(lags):.0f} ms max {max(lags)} ms')
PY
grep -E "Memory peak|Service runtime|CPU time|exit" "$OUT/log.txt" || true; cat "$OUT/mem.txt"
