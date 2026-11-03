#!/usr/bin/env bash
# Bounded Vulkan CTS batches on the tablet.
#   cts.sh setup | cleanup                       push / remove deqp-vk and its data
#   cts.sh run --icd ICD --run RUN NAME CASELIST  one case list
#   cts.sh mustpass ICD RUN [STRIDE]              every vk-default group; with
#                                                 STRIDE, every STRIDE-th case only
#   cts.sh ledger RUN...                          markdown tables
# Results: $RADISH_SRC/cts-runs/RUN/<group>/{cases.txt,results.tsv,batches.tsv,qpa/}
set -euo pipefail
HERE=$(dirname "$(realpath "$0")")
SRC=${RADISH_SRC:-/run/media/jethac/Shared/radish-src}
MUSTPASS=$SRC/vk-gl-cts/external/vulkancts/mustpass/main
if [[ ${1:-} == mustpass ]]; then
    icd=$2 run=$3 stride=${4:-1}
    lists=$SRC/cts-lists/stride$stride
    mkdir -p "$lists"
    while read -r f; do
        n=${f#vk-default/}; n=${n%.txt}; n=${n//\//.}
        awk -v s="$stride" 'NR % s == 1 || s == 1' "$MUSTPASS/$f" > "$lists/$n.txt"
    done < "$MUSTPASS/vk-default.txt"
    exec "$HERE/cts-run-list.sh" "$run" "$icd" "$lists"
fi
exec python3 "$HERE/cts.py" "$@"
