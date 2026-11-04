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

# Batches run only on the baseline kernel (the device is shared with kernel
# work). Call inside the lock; non-zero means release the lock and retry later.
BASELINE_KERNEL=${RADISH_BASELINE_KERNEL:-'Linux version 7.3.0-g755ffc44d5d8-dirty (jethac@xps17-9700) (clang version 23.1.3, LLD 23.1.3) #7 SMP PREEMPT Tue Nov  3 19:34:20 JST 2026'}
on_baseline() {
    local v; v=$(adbsh 'cat /proc/version' 2>/dev/null | tr -d '\r')
    if [[ "$v" != "$BASELINE_KERNEL" ]]; then
        echo "device not on the baseline kernel: $v ($(adbsh 'cut -d" " -f1 /proc/uptime' 2>/dev/null))" >&2
        return 1
    fi
}
