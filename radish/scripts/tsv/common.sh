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
# Baseline = the version string of the accepted build on the laptop.
BASELINE_IMAGE=${RADISH_BASELINE_IMAGE:-/home/jethac/Projects/lenovo-8/kernel/build-7.3/arch/arm64/boot/Image}
baseline_kernel() {
    if [[ -n ${RADISH_BASELINE_KERNEL:-} ]]; then echo "$RADISH_BASELINE_KERNEL"; return; fi
    strings "$BASELINE_IMAGE" 2>/dev/null | grep -m1 -E '^Linux version .*#[0-9]+ SMP PREEMPT'
}
on_baseline() {
    local v; v=$(adbsh 'cat /proc/version' 2>/dev/null | tr -d '\r')
    local want; want=$(baseline_kernel)
    if [[ -z "$want" || "$v" != "$want" ]]; then
        echo "device not on the baseline kernel: $v ($(adbsh 'cut -d" " -f1 /proc/uptime' 2>/dev/null))" >&2
        return 1
    fi
}
