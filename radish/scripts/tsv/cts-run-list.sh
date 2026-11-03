#!/usr/bin/env bash
# cts-run-list.sh RUN ICD LISTDIR: run every LISTDIR/<group>.txt in vk-default.txt
# order (group = mustpass file path with / -> .). Stops at a GPU fault that the
# runner cannot clear (see cts.py --allow-page-faults, set through CTS_FLAGS).
set -euo pipefail
RUN=$1 ICD=$2 L=$3
M=${RADISH_SRC:-/run/media/jethac/Shared/radish-src}/vk-gl-cts/external/vulkancts/mustpass/main
for f in $(cat $M/vk-default.txt); do
  n=${f#vk-default/}; n=${n%.txt}; n=${n//\//.}
  [[ -s $L/$n.txt ]] || continue
  python3 "$(dirname "$(realpath "$0")")/cts.py" run ${CTS_FLAGS:-} --icd "$ICD" --run "$RUN" "$n" "$L/$n.txt" | { grep --line-buffered -v '^|' || true; }
done
echo ALLDONE
