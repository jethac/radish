# Shared helpers for the tablet scripts. Source this file.
SERIAL=${RADISH_SERIAL:-feb0bd1f028e4cb0}
LOCK=${RADISH_DEVICE_LOCK:-/home/jethac/Projects/lenovo-8/.device.lock}
SRC=${RADISH_SRC:-/run/media/jethac/Shared/radish-src}
PREFIX=/var/tmp/radish-canon
adbsh() { adb -s "$SERIAL" shell "$@"; }
# Count of GPU fault/hang/recovery lines in the kernel log.
gpu_fault_count() {
    adbsh 'dmesg | grep -v audit | grep -ciE "hangcheck|gpu fault|iommu fault|smmu|recover_worker|offending task"'
}
