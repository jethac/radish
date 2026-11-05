#!/usr/bin/env python3
"""Bounded Vulkan CTS batches on the tablet.

The runner pushes one stripped deqp-vk and the CTS data into
/var/tmp/radish-canon/cts, then runs case lists in batches. Each batch is
one locked session: fault count, one memory-capped transient unit with a
watchdog, fault count again, pull TestResults.qpa, delete it on the device.
A batch that dies (crash, kill, timeout) is resumed at the next case not yet
reported; the case that was running is recorded as Crash or Timeout.

Results live on the laptop under $RADISH_SRC/cts-runs/<run>/<name>/:
  cases.txt       the case list of this group
  results.tsv     case <TAB> status <TAB> details, one line per case
  batches.tsv     batch number, cases, seconds, outcome, faults before/after
  qpa/NNNN.qpa.xz raw logs (laptop only)

Usage:
  cts.py setup                         push deqp-vk + data (idempotent)
  cts.py run --icd ICD --run R NAME CASELIST [--target-seconds S]
  cts.py summary RUN...                print per-group tables
  cts.py cleanup                       remove the CTS from the device
"""
import argparse
import contextlib
import fcntl
import collections
import hashlib
import lzma
import os
import re
import shlex
import subprocess
import sys
import time

SERIAL = os.environ.get("RADISH_SERIAL", "feb0bd1f028e4cb0")
LOCK = os.environ.get("RADISH_DEVICE_LOCK", "/home/jethac/Projects/lenovo-8/.device.lock")
LIVE_LOG = os.environ.get("RADISH_LIVE_LOG",
    os.path.join(os.environ.get("RADISH_SRC", "/run/media/jethac/Shared/radish-src"), "cts-runs", "live.log"))
SRC = os.environ.get("RADISH_SRC", "/run/media/jethac/Shared/radish-src")
PREFIX = "/var/tmp/radish-canon"
DEV = PREFIX + "/cts"
CTS_SRC = SRC + "/vk-gl-cts"
CTS_BIN = SRC + "/cts-deploy/deqp-vk"
RUNS = SRC + "/cts-runs"
STATUSES = ["Pass", "Fail", "NotSupported", "QualityWarning", "CompatibilityWarning",
            "Crash", "Timeout", "InternalError", "ResourceError", "DeviceLost", "Waiver",
            "Missing"]
FAULT_RE = "hangcheck|gpu fault|iommu fault|smmu|recover_worker|offending task"


def adb(*args, check=True, capture=True, timeout=None):
    return subprocess.run(["adb", "-s", SERIAL, *args], check=check, timeout=timeout,
                          stdout=subprocess.PIPE if capture else None,
                          stderr=subprocess.STDOUT if capture else None, text=True)


def adbsh(cmd, check=True, timeout=None):
    return adb("shell", cmd, check=check, timeout=timeout).stdout


def fault_lines():
    out = adbsh(f"dmesg | grep -v audit | grep -iE '{FAULT_RE}'", check=False)
    return [l for l in out.splitlines() if l.strip()]


# The device is shared with kernel work: batches run only on the baseline
# kernel. If another kernel is booted, the lock is released and taken again
# later instead of running.
BASELINE_IMAGE = os.environ.get(
    "RADISH_BASELINE_IMAGE", "/home/jethac/Projects/lenovo-8/kernel/build-7.3/arch/arm64/boot/Image")


def baseline_kernel():
    """The version string of the accepted kernel build on the laptop, read
    from its Image each time (so a new baseline is picked up), or
    RADISH_BASELINE_KERNEL if set."""
    if os.environ.get("RADISH_BASELINE_KERNEL"):
        return os.environ["RADISH_BASELINE_KERNEL"]
    try:
        with open(BASELINE_IMAGE, "rb") as f:
            data = f.read()
    except OSError:
        return None
    m = re.search(rb"Linux version [^\0\n]*#[0-9]+ SMP PREEMPT[^\0\n]*", data)
    return m.group(0).decode(errors="replace").strip() if m else None


def device_state():
    out = adbsh("cat /proc/version; cut -d' ' -f1 /proc/uptime", check=False).splitlines()
    version = out[0].strip() if out else ""
    uptime = float(out[1]) if len(out) > 1 and out[1].strip() else -1.0
    return version, uptime


@contextlib.contextmanager
def device_lock(timeout=1800, wait_baseline=4 * 3600):
    """flock(2) on the shared lock file, the same lock flock(1) takes, held
    only while the baseline kernel is running."""
    fd = os.open(LOCK, os.O_RDWR | os.O_CREAT, 0o666)
    t0 = time.time()
    while True:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            if time.time() - t0 > timeout + wait_baseline:
                os.close(fd)
                sys.exit("could not take the device lock")
            time.sleep(1)
            continue
        try:
            version, uptime = device_state()
        except Exception:
            version, uptime = "", -1.0
        if version and version == baseline_kernel() and uptime >= 0:
            break
        fcntl.flock(fd, fcntl.LOCK_UN)
        print(f"device not on the baseline kernel ({version[:90]!r}, uptime {uptime}); waiting",
              file=sys.stderr, flush=True)
        if time.time() - t0 > timeout + wait_baseline:
            os.close(fd)
            sys.exit("device not on the baseline kernel")
        time.sleep(60)
    try:
        yield
    finally:
        fcntl.flock(fd, fcntl.LOCK_UN)
        os.close(fd)


def locked(argv):
    """Re-exec argv under the device lock (one batch per lock hold)."""
    return subprocess.run(["flock", "-w", "1800", LOCK, *argv])


def cmd_setup(args):
    if not os.path.exists(CTS_BIN):
        sys.exit(f"missing {CTS_BIN}")
    sha = hashlib.sha256(open(CTS_BIN, "rb").read()).hexdigest()
    have = adbsh(f"sha256sum {DEV}/deqp-vk 2>/dev/null", check=False).split()
    adbsh(f"mkdir -p {DEV}/run")
    if not have or have[0] != sha:
        adb("push", "-q", CTS_BIN, f"{DEV}/deqp-vk.tmp")
        adbsh(f"chmod 755 {DEV}/deqp-vk.tmp && mv {DEV}/deqp-vk.tmp {DEV}/deqp-vk")
    data = f"{CTS_SRC}/external/vulkancts/data/vulkan"
    if adbsh(f"test -d {DEV}/vulkan && echo yes", check=False).strip() != "yes":
        # The video samples are not used (video tests are disabled in this build).
        tar = subprocess.Popen(["tar", "-C", data, "--exclude=./video", "-cf", "-", "."],
                               stdout=subprocess.PIPE)
        subprocess.run(["adb", "-s", SERIAL, "shell",
                        f"mkdir -p {DEV}/vulkan.tmp && tar -C {DEV}/vulkan.tmp -xf - && "
                        f"mv {DEV}/vulkan.tmp {DEV}/vulkan"], stdin=tar.stdout, check=True)
        tar.wait()
    print(adbsh(f"sha256sum {DEV}/deqp-vk; du -sh {DEV}; df -h /var/tmp | tail -1"))


def cmd_cleanup(args):
    print(adbsh(f"rm -rf {DEV}; df -h /var/tmp | tail -1"))


# --- qpa parsing -----------------------------------------------------------

BEGIN = "#beginTestCaseResult "
RESULT_RE = re.compile(r'<Result StatusCode="([A-Za-z]+)">(.*?)</Result>', re.S)
TEXT_RE = re.compile(r"<Text>([^<]{1,200})", re.S)
GFX_STAGE_RE = re.compile(r"<(VertexShader|FragmentShader|GeometryShader|TessControlShader|"
                          r"TessEvaluationShader|MeshShader|TaskShader)\b|Section Name=\"[^\"]*"
                          r"(vertex|fragment|geometry|tess)[^\"]*_shader\"")


def parse_qpa(text):
    """Return (results dict case -> (status, detail), unfinished case or None)."""
    results = {}
    current = None
    body = []
    for line in text.splitlines():
        if line.startswith(BEGIN):
            current = line[len(BEGIN):].strip()
            body = []
        elif line.startswith("#endTestCaseResult"):
            if current:
                text = "\n".join(body)
                m = RESULT_RE.search(text)
                status, detail = (m.group(1), m.group(2)) if m else ("InternalError", "no result")
                if status not in ("Pass", "NotSupported"):
                    # Terse results ("Fail"): add the first log message.
                    t = TEXT_RE.search(text)
                    if t and len(detail) < 20:
                        detail = f"{detail}: {t.group(1)}"
                    # Mark cases whose programs include graphics stages.
                    if GFX_STAGE_RE.search(text):
                        detail = "[gfx] " + detail
                results[current] = (status, detail)
            current = None
        elif line.startswith("#terminateTestCaseResult"):
            if current:
                reason = line.split(None, 1)[1].strip() if " " in line else "Crash"
                results[current] = (reason if reason in STATUSES else "Crash", "terminated: " + reason)
            current = None
        elif current is not None:
            body.append(line)
    return results, current


def clean_detail(d):
    d = re.sub(r"\s+", " ", d.replace("&quot;", '"').replace("&lt;", "<")
               .replace("&gt;", ">").replace("&amp;", "&")).strip()
    return d[:300]


# --- one batch -----------------------------------------------------------------

def run_batch(icd, cases, runtime_max, env=()):
    """Run one batch on the device. Caller holds the lock."""
    boot_id = adbsh("cat /proc/sys/kernel/random/boot_id", check=False).strip()
    before = fault_lines()
    listing = "\n".join(cases) + "\n"
    local = os.path.join(SRC, "cts-deploy", "batch-cases.txt")
    with open(local, "w") as f:
        f.write(listing)
    adbsh(f"rm -rf {DEV}/run && mkdir -p {DEV}/run")
    adb("push", "-q", local, f"{DEV}/run/cases.txt")
    deqp = " ".join([
        f"{DEV}/deqp-vk",
        f"--deqp-caselist-file={DEV}/run/cases.txt",
        f"--deqp-log-filename={DEV}/run/TestResults.qpa",
        f"--deqp-archive-dir={DEV}",
        f"--deqp-shadercache-filename={DEV}/run/shadercache.bin",
        "--deqp-log-images=disable",
        "--deqp-log-shader-sources=disable",
        "--deqp-log-decompiled-spirv=disable",
        "--deqp-log-empty-loginfo=disable",
        "--deqp-watchdog=enable",
        "--deqp-watchdog-total-time-limit=300",
        "--deqp-watchdog-interval-time-limit=150",
    ])
    unit = " ".join([
        "systemd-run --wait --pipe --collect",
        "-p MemoryMax=400M -p MemorySwapMax=64M", f"-p RuntimeMaxSec={runtime_max}",
        "-p LimitCORE=0 -p Nice=10", f"-p WorkingDirectory={DEV}/run",
        f"--setenv=VK_DRIVER_FILES={icd}", f"--setenv=LD_LIBRARY_PATH={PREFIX}/lib",
        f"--setenv=VK_LOADER_LAYERS_DISABLE=all",
        *[f"--setenv={shlex.quote(e)}" for e in env],
    ])
    # Timestamp each "Test case" line with the kernel's monotonic clock so a
    # fault line in dmesg can be matched to the case that was running.
    stamp = ('while IFS= read -r l; do case "$l" in "Test case"*|*Fail*|*Crash*|*Timeout*|radish:*|*"Finished with result"*|*"Main processes terminated"*|*"Memory peak"*|*"Service runtime"*)'
             ' read -r up _ </proc/uptime; printf "%s %s\\n" "$up" "$l";; esac; done')
    # The stamped lines are also streamed to the host while the batch runs
    # (LIVE_LOG), so a device wedge leaves the last case on the host.
    script = (f"{unit} {deqp} 2>&1 | {stamp} | tee {DEV}/run/stdout.txt; "
              f"echo rc=${{PIPESTATUS[0]}}")
    t0 = time.time()
    live = open(LIVE_LOG, "a") if LIVE_LOG else None
    if live:
        live.write(f"=== batch start {time.strftime('%F %T')} ({len(cases)} cases)\n")
        live.flush()
    proc = subprocess.Popen(["adb", "-s", SERIAL, "shell", f"bash -c {shlex.quote(script)}"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    chunks = []
    try:
        for line in proc.stdout:
            chunks.append(line)
            if live:
                live.write(line)
                live.flush()
            if time.time() - t0 > runtime_max + 120:
                proc.kill()
                break
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()
    if live:
        live.write(f"=== batch end {time.strftime('%F %T')}\n")
        live.close()
    out = "".join(chunks)
    # A new boot ID (the hardware watchdog rebooted a wedged kernel) or a
    # lost device mid-batch is a wedge: report and stop, never retry.
    for _ in range(30):
        now_id = adbsh("cat /proc/sys/kernel/random/boot_id", check=False).strip()
        if now_id and "error" not in now_id:
            break
        time.sleep(10)
    if not now_id or now_id != boot_id:
        last = [l for l in chunks if "Test case" in l][-1:] or ["(no case line)"]
        msg = (f"WEDGE: boot id {boot_id} -> {now_id or 'device gone'} during a batch; "
               f"last case: {last[0].strip()}; see {LIVE_LOG} and the dmesg-follow log")
        print(msg, flush=True)
        with open(os.path.join(SRC, "cts-runs", "WEDGE.txt"), "a") as f:
            f.write(time.strftime("%F %T ") + msg + "\n")
        sys.exit(3)
    seconds = time.time() - t0
    rc = re.search(r"rc=(\d+)", out)
    rc = int(rc.group(1)) if rc else -1
    qpa = adbsh(f"cat {DEV}/run/TestResults.qpa 2>/dev/null", check=False)
    stdout = adbsh(f"cat {DEV}/run/stdout.txt 2>/dev/null", check=False)
    after = fault_lines()
    adbsh(f"rm -rf {DEV}/run")
    new_faults = [l for l in after if l not in before]
    return dict(rc=rc, seconds=seconds, qpa=qpa, stdout=stdout, faults_before=len(before),
                faults_after=len(after), new_faults=new_faults, out=out)


def case_at(stdout, ts):
    """Name of the case whose 'Test case' line is the last one before ts."""
    best = None
    for line in stdout.splitlines():
        m = re.match(r"([\d.]+) Test case '([^']+)'", line)
        if m and float(m.group(1)) <= ts:
            best = m.group(2)
    return best


def cmd_run(args):
    outdir = os.path.join(RUNS, args.run, args.name)
    os.makedirs(os.path.join(outdir, "qpa"), exist_ok=True)
    cases = [l.strip() for l in open(args.caselist) if l.strip() and not l.startswith("#")]
    with open(os.path.join(outdir, "cases.txt"), "w") as f:
        f.write("\n".join(cases) + "\n")
    res_path = os.path.join(outdir, "results.tsv")
    done = {}
    if os.path.exists(res_path):
        for line in open(res_path):
            parts = line.rstrip("\n").split("\t")
            done[parts[0]] = parts[1]
    excluded = set()
    if args.exclude:
        excluded = {l.strip() for l in open(args.exclude) if l.strip()}
    batches_path = os.path.join(outdir, "batches.tsv")
    nbatch = sum(1 for _ in open(batches_path)) if os.path.exists(batches_path) else 0
    resf = open(res_path, "a")
    batf = open(batches_path, "a")
    # reparse rewrites results.tsv; it skips a group while this is present.
    running = os.path.join(outdir, ".running")
    open(running, "w").write(str(os.getpid()))
    try:
        _run_loop(args, outdir, cases, done, excluded, nbatch, resf, batf)
    finally:
        os.unlink(running)
    summarize([outdir])


def _run_loop(args, outdir, cases, done, excluded, nbatch, resf, batf):
    rate = None  # seconds per case, measured
    fault_streak = False
    size = args.first_batch
    while True:
        todo = [c for c in cases if c not in done and c not in excluded]
        if not todo:
            break
        if rate:
            size = int(max(args.min_batch, min(args.max_batch, args.target_seconds / rate)))
        batch = todo[:size]
        nbatch += 1
        with device_lock():
            r = run_batch(args.icd, batch, args.runtime_max, args.env)
        with lzma.open(os.path.join(outdir, "qpa", f"{nbatch:04d}.qpa.xz"), "wt") as f:
            f.write(r["qpa"])
        with lzma.open(os.path.join(outdir, "qpa", f"{nbatch:04d}.stdout.xz"), "wt") as f:
            f.write(r["stdout"] + "\n--- adb ---\n" + r["out"])
        results, unfinished = parse_qpa(r["qpa"])
        batchset = set(batch)
        reported = 0
        for case, (status, detail) in results.items():
            if case in batchset and case not in done:
                done[case] = status
                resf.write(f"{case}\t{status}\t{clean_detail(detail)}\n")
                reported += 1
        outcome = "ok"
        if unfinished and unfinished not in done:
            status = "Timeout" if "timeout" in r["out"].lower() else "Crash"
            done[unfinished] = status
            resf.write(f"{unfinished}\t{status}\tprocess ended in this case (rc={r['rc']})\n")
            outcome = status.lower() + ":" + unfinished
        elif reported == 0:
            # Nothing ran: report the whole batch as missing rather than loop.
            for case in batch:
                if case not in done:
                    done[case] = "Missing"
                    resf.write(f"{case}\tMissing\tnot executed (rc={r['rc']})\n")
            outcome = f"nothing-ran:rc={r['rc']}"
        elif r["rc"] == 0:
            for case in batch:
                if case not in done:
                    done[case] = "Missing"
                    resf.write(f"{case}\tMissing\tnot in binary\n")
        resf.flush()
        executed = max(1, reported + (1 if unfinished else 0))
        if outcome == "ok" or not outcome.startswith("timeout"):
            rate = r["seconds"] / executed if not rate else 0.5 * rate + 0.5 * r["seconds"] / executed
        batf.write(f"{nbatch}\t{len(batch)}\t{reported}\t{r['seconds']:.1f}\t{outcome}\t"
                   f"{r['faults_before']}\t{r['faults_after']}\n")
        batf.flush()
        remaining = len([c for c in cases if c not in done and c not in excluded])
        print(f"[{args.name}] batch {nbatch}: {reported}/{len(batch)} in {r['seconds']:.0f}s "
              f"{outcome}; faults {r['faults_before']}->{r['faults_after']}; "
              f"{remaining} left", flush=True)
        if r["new_faults"]:
            wedged = any(re.search("hangcheck|recover|offending", l, re.I) for l in r["new_faults"])
            last_ts, culprits = 0.0, []
            with open(os.path.join(outdir, "FAULT.txt"), "a") as f:
                f.write(f"batch {nbatch}\n")
                for line in r["new_faults"]:
                    m = re.match(r"\[\s*([\d.]+)\]", line)
                    who = case_at(r["stdout"], float(m.group(1))) if m else None
                    if m:
                        last_ts = max(last_ts, float(m.group(1)))
                    culprits.append(who)
                    f.write(f"{line}\n  case running: {who}\n")
            # The GPU recovered if a case started after the last fault passed.
            later = [mm.group(2) for mm in re.finditer(r"([\d.]+) Test case '([^']+)'", r["stdout"])
                     if float(mm.group(1)) > last_ts]
            recovered = any(done.get(c) == "Pass" for c in later)
            with open(os.path.join(RUNS, args.run, "faulting-cases.txt"), "a") as f:
                for who in culprits:
                    f.write(f"{who}\t{args.name}\tbatch {nbatch}\t"
                            f"{'wedged' if wedged else ('recovered' if recovered else 'unknown')}\n")
            print(open(os.path.join(outdir, "FAULT.txt")).read(), flush=True)
            # A page fault with no hangcheck is not a hang. Continue (when
            # allowed) unless the previous batch also faulted and nothing
            # in this one passed.
            passed_any = any(done.get(c) == "Pass" for c in batch)
            if wedged or not args.allow_page_faults or (fault_streak and not passed_any):
                sys.exit(f"[{args.name}] new GPU fault lines; stopping. See {outdir}/FAULT.txt")
            fault_streak = True
            print(f"[{args.name}] page fault without hang "
                  f"({'a later case passed' if recovered else 'no later pass in batch'}); continuing",
                  flush=True)
        else:
            fault_streak = False


# --- summaries -----------------------------------------------------------------

def load_results(path):
    res = {}
    for line in open(path):
        parts = line.rstrip("\n").split("\t")
        if len(parts) >= 2:
            res[parts[0]] = parts[1]
    return res


def summarize(dirs, depth=3, file=sys.stdout):
    cols = ["Pass", "Fail", "NotSupported", "QualityWarning", "CompatibilityWarning",
            "Crash", "Timeout", "InternalError"]
    for d in dirs:
        cases = [l.strip() for l in open(os.path.join(d, "cases.txt")) if l.strip()]
        res = load_results(os.path.join(d, "results.tsv"))
        groups = collections.OrderedDict()
        for c in cases:
            key = ".".join(c.split(".")[:depth])
            g = groups.setdefault(key, collections.Counter())
            g[res.get(c, "NotRun")] += 1
        print("| group | cases | " + " | ".join(cols) + " | other | not run |", file=file)
        print("|---" * (len(cols) + 4) + "|", file=file)
        for key, g in groups.items():
            other = sum(v for k, v in g.items() if k not in cols and k != "NotRun")
            print(f"| {key} | {sum(g.values())} | " + " | ".join(str(g[c]) for c in cols)
                  + f" | {other} | {g['NotRun']} |", file=file)


def cmd_summary(args):
    summarize(args.dirs, args.depth)


GRAPHICS_DETAIL = re.compile(
    r"createGraphicsPipelines|endCommandBuffer\(commandBuffer\): VK_ERROR_FEATURE_NOT_PRESENT|"
    r"createRenderPass|createFramebuffer|Graphics queue|"
    r"retcode: VK_ERROR_FEATURE_NOT_PRESENT at vkPipelineConstructionUtil")
GRAPHICS_PATH = re.compile(
    r"\.(vertex|fragment|geometry|tess_control|tess_eval|tessellation|vert|frag|geom|tesc|tese|"
    r"graphics|draw|draw_indexed|render_pass|renderpass)(\.|_|$)|_(graphics|vertex|fragment|frag|vert)(\.|_|$)")
CTS_QUEUE_DETAIL = re.compile(
    r"createDeviceInternal\(.*VK_ERROR_INITIALIZATION_FAILED|Returned queue family count was 0")


def classify(case, status, detail):
    """Bucket for a result that is not Pass/NotSupported/Warning."""
    if CTS_QUEUE_DETAIL.search(detail):
        return "cts-queue"
    if GRAPHICS_DETAIL.search(detail):
        return "graphics-path"
    compute_case = re.search(r"(\.|_)comp(ute)?(\.|_|$)", case)
    if detail.startswith("[gfx]") and not compute_case:
        return "graphics-path"
    if compute_case:
        return "other"
    if GRAPHICS_PATH.search(case):
        return "graphics-path"
    return "other"


def cmd_reparse(args):
    """Rebuild results.tsv details from the kept qpa logs (statuses that came
    from the runner itself, Crash/Timeout/Missing, are kept)."""
    for run in args.runs:
        for name in sorted(os.listdir(os.path.join(RUNS, run))):
            d = os.path.join(RUNS, run, name)
            path = os.path.join(d, "results.tsv")
            if not os.path.exists(path):
                continue
            if os.path.exists(os.path.join(d, ".running")):
                print(f"skipping {run}/{name}: still running", file=sys.stderr)
                continue
            parsed = {}
            for q in sorted(os.listdir(os.path.join(d, "qpa"))):
                if q.endswith(".qpa.xz"):
                    res, _ = parse_qpa(lzma.open(os.path.join(d, "qpa", q), "rt").read())
                    parsed.update(res)
            out = []
            for line in open(path):
                parts = line.rstrip("\n").split("\t")
                if parts[0] in parsed and parsed[parts[0]][0] == parts[1]:
                    parts = [parts[0], parts[1], clean_detail(parsed[parts[0]][1])]
                out.append("\t".join(parts) + "\n")
            open(path + ".tmp", "w").writelines(out)
            os.replace(path + ".tmp", path)


def cmd_classify(args):
    """Per top-level group: non-pass results split into graphics-path (the
    case needs a graphics queue or graphics pipeline), cts-queue (the CTS
    asks for queue families a compute-only device cannot give) and other
    (candidate driver failures). With --list, print the 'other' cases."""
    buckets = collections.defaultdict(collections.Counter)
    others = []
    merged = {}  # later runs override earlier ones, case by case
    for run in args.runs:
        for name in sorted(os.listdir(os.path.join(RUNS, run))):
            path = os.path.join(RUNS, run, name, "results.tsv")
            if not os.path.exists(path):
                continue
            for line in open(path):
                parts = line.rstrip("\n").split("\t")
                merged[parts[0]] = (parts[1], parts[2] if len(parts) > 2 else "")
    for case, (status, detail) in merged.items():
        if status in ("Pass", "NotSupported", "QualityWarning", "CompatibilityWarning"):
            continue
        b = classify(case, status, detail)
        buckets[".".join(case.split(".")[:args.depth])][b] += 1
        if b == "other":
            others.append((case, status, detail))
    if args.list:
        for c, st, d in others:
            print(f"{c}\t{st}\t{d}")
        return
    print("| group | graphics-path | cts-queue | other |")
    print("|---|---|---|---|")
    tot = collections.Counter()
    for g, c in sorted(buckets.items()):
        tot.update(c)
        print(f"| {g} | {c['graphics-path']} | {c['cts-queue']} | {c['other']} |")
    print(f"| **total** | {tot['graphics-path']} | {tot['cts-queue']} | {tot['other']} |")


def cmd_ledger(args):
    """Markdown tables: top-level groups, then second-level groups, over every
    group directory of the given runs (later runs override earlier ones per case)."""
    cols = ["Pass", "Fail", "NotSupported", "QualityWarning", "CompatibilityWarning",
            "Crash", "Timeout", "InternalError"]
    res, cases = {}, []
    seen = set()
    source = {}  # case -> run that supplied its result
    for run in args.runs:
        for name in sorted(os.listdir(os.path.join(RUNS, run))):
            d = os.path.join(RUNS, run, name)
            if not os.path.exists(os.path.join(d, "results.tsv")):
                continue
            for c in load_results(os.path.join(d, "results.tsv")):
                source[c] = run
            for c in open(os.path.join(d, "cases.txt")):
                c = c.strip()
                if c and c not in seen:
                    seen.add(c)
                    cases.append(c)
            res.update(load_results(os.path.join(d, "results.tsv")))
    if args.all_cases:
        allc = [l.strip() for l in open(args.all_cases) if l.strip()]
        for c in allc:
            if c not in seen:
                seen.add(c)
                cases.append(c)
    for depth in (2, 3):
        groups = collections.OrderedDict()
        runs_of = collections.defaultdict(set)
        for c in cases:
            key = ".".join(c.split(".")[:depth])
            g = groups.setdefault(key, collections.Counter())
            g[res.get(c, "NotRun")] += 1
            if c in source:
                runs_of[key].add(source[c].rsplit("-", 1)[-1])
        tot = collections.Counter()
        print(f"\n| {'group' if depth == 2 else 'second-level group'} | cases | "
              + " | ".join(cols) + " | other | not run | driver |")
        print("|---" * (len(cols) + 5) + "|")
        for key, g in sorted(groups.items()):
            if depth == 3 and args.skip_all_notsupported and g["NotSupported"] == sum(g.values()):
                continue
            tot.update(g)
            other = sum(v for k, v in g.items() if k not in cols and k != "NotRun")
            print(f"| {key} | {sum(g.values())} | " + " | ".join(str(g[c]) for c in cols)
                  + f" | {other} | {g['NotRun']} | {', '.join(sorted(runs_of[key]))} |")
        if depth == 2:
            other = sum(v for k, v in tot.items() if k not in cols and k != "NotRun")
            print(f"| **total** | {sum(tot.values())} | " + " | ".join(str(tot[c]) for c in cols)
                  + f" | {other} | {tot['NotRun']} | |")


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("setup")
    sub.add_parser("cleanup")
    r = sub.add_parser("run")
    r.add_argument("--icd", required=True)
    r.add_argument("--run", required=True)
    r.add_argument("--exclude")
    r.add_argument("--env", action="append", default=[], help="VAR=VALUE for the test process")
    r.add_argument("--allow-page-faults", action="store_true",
                   help="continue after an IOMMU page fault (no hangcheck) when a later "
                        "case in the same batch passed")
    r.add_argument("--target-seconds", type=float, default=120)
    r.add_argument("--first-batch", type=int, default=300)
    r.add_argument("--min-batch", type=int, default=20)
    r.add_argument("--max-batch", type=int, default=6000)
    r.add_argument("--runtime-max", type=int, default=600)
    r.add_argument("name")
    r.add_argument("caselist")
    s = sub.add_parser("summary")
    s.add_argument("--depth", type=int, default=3)
    s.add_argument("dirs", nargs="+")
    rp = sub.add_parser("reparse")
    rp.add_argument("runs", nargs="+")
    c = sub.add_parser("classify")
    c.add_argument("--depth", type=int, default=2)
    c.add_argument("--list", action="store_true")
    c.add_argument("runs", nargs="+")
    l = sub.add_parser("ledger")
    l.add_argument("--all-cases")
    l.add_argument("--skip-all-notsupported", action="store_true")
    l.add_argument("runs", nargs="+")
    args = p.parse_args()
    {"setup": lambda a: locked([sys.executable, __file__, "_setup"]),
     "cleanup": lambda a: locked([sys.executable, __file__, "_cleanup"]),
     "run": cmd_run, "summary": cmd_summary, "ledger": cmd_ledger, "classify": cmd_classify, "reparse": cmd_reparse}[args.cmd](args)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("_setup", "_cleanup"):
        {"_setup": cmd_setup, "_cleanup": cmd_cleanup}[sys.argv[1]](None)
    else:
        main()
