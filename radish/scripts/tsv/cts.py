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


@contextlib.contextmanager
def device_lock(timeout=1800):
    """flock(2) on the shared lock file, the same lock flock(1) takes."""
    fd = os.open(LOCK, os.O_RDWR | os.O_CREAT, 0o666)
    t0 = time.time()
    while True:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except BlockingIOError:
            if time.time() - t0 > timeout:
                os.close(fd)
                sys.exit("could not take the device lock")
            time.sleep(1)
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
                m = RESULT_RE.search("\n".join(body))
                status, detail = (m.group(1), m.group(2)) if m else ("InternalError", "no result")
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

def run_batch(icd, cases, runtime_max):
    """Run one batch on the device. Caller holds the lock."""
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
        "--deqp-watchdog-total-time-limit=120",
        "--deqp-watchdog-interval-time-limit=60",
    ])
    unit = " ".join([
        "systemd-run --wait --pipe --collect --quiet",
        "-p MemoryMax=400M -p MemorySwapMax=64M", f"-p RuntimeMaxSec={runtime_max}",
        "-p LimitCORE=0 -p Nice=10", f"-p WorkingDirectory={DEV}/run",
        f"--setenv=VK_DRIVER_FILES={icd}", f"--setenv=LD_LIBRARY_PATH={PREFIX}/lib",
        f"--setenv=VK_LOADER_LAYERS_DISABLE=all",
    ])
    # Timestamp each "Test case" line with the kernel's monotonic clock so a
    # fault line in dmesg can be matched to the case that was running.
    stamp = ('while IFS= read -r l; do case "$l" in "Test case"*|*Fail*|*Crash*|*Timeout*)'
             ' read -r up _ </proc/uptime; printf "%s %s\\n" "$up" "$l";; esac; done')
    script = (f"{unit} {deqp} 2>&1 | {stamp} > {DEV}/run/stdout.txt; "
              f"echo rc=${{PIPESTATUS[0]}}")
    t0 = time.time()
    out = adbsh(f"bash -c {shlex.quote(script)}", check=False, timeout=runtime_max + 120)
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
    rate = None  # seconds per case, measured
    size = args.first_batch
    resf = open(res_path, "a")
    batf = open(batches_path, "a")
    while True:
        todo = [c for c in cases if c not in done and c not in excluded]
        if not todo:
            break
        if rate:
            size = int(max(args.min_batch, min(args.max_batch, args.target_seconds / rate)))
        batch = todo[:size]
        nbatch += 1
        with device_lock():
            r = run_batch(args.icd, batch, args.runtime_max)
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
            if wedged or not recovered or not args.allow_page_faults:
                sys.exit(f"[{args.name}] new GPU fault lines; stopping. See {outdir}/FAULT.txt")
            print(f"[{args.name}] page fault without hang; later case passed; continuing", flush=True)
    summarize([outdir])


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


def cmd_ledger(args):
    """Markdown tables: top-level groups, then second-level groups, over every
    group directory of the given runs (later runs override earlier ones per case)."""
    cols = ["Pass", "Fail", "NotSupported", "QualityWarning", "CompatibilityWarning",
            "Crash", "Timeout", "InternalError"]
    res, cases = {}, []
    seen = set()
    for run in args.runs:
        for name in sorted(os.listdir(os.path.join(RUNS, run))):
            d = os.path.join(RUNS, run, name)
            if not os.path.exists(os.path.join(d, "results.tsv")):
                continue
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
        for c in cases:
            g = groups.setdefault(".".join(c.split(".")[:depth]), collections.Counter())
            g[res.get(c, "NotRun")] += 1
        tot = collections.Counter()
        print(f"\n| {'group' if depth == 2 else 'second-level group'} | cases | "
              + " | ".join(cols) + " | other | not run |")
        print("|---" * (len(cols) + 4) + "|")
        for key, g in sorted(groups.items()):
            if depth == 3 and args.skip_all_notsupported and g["NotSupported"] == sum(g.values()):
                continue
            tot.update(g)
            other = sum(v for k, v in g.items() if k not in cols and k != "NotRun")
            print(f"| {key} | {sum(g.values())} | " + " | ".join(str(g[c]) for c in cols)
                  + f" | {other} | {g['NotRun']} |")
        if depth == 2:
            other = sum(v for k, v in tot.items() if k not in cols and k != "NotRun")
            print(f"| **total** | {sum(tot.values())} | " + " | ".join(str(tot[c]) for c in cols)
                  + f" | {other} | {tot['NotRun']} |")


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("setup")
    sub.add_parser("cleanup")
    r = sub.add_parser("run")
    r.add_argument("--icd", required=True)
    r.add_argument("--run", required=True)
    r.add_argument("--exclude")
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
    l = sub.add_parser("ledger")
    l.add_argument("--all-cases")
    l.add_argument("--skip-all-notsupported", action="store_true")
    l.add_argument("runs", nargs="+")
    args = p.parse_args()
    {"setup": lambda a: locked([sys.executable, __file__, "_setup"]),
     "cleanup": lambda a: locked([sys.executable, __file__, "_cleanup"]),
     "run": cmd_run, "summary": cmd_summary, "ledger": cmd_ledger}[args.cmd](args)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("_setup", "_cleanup"):
        {"_setup": cmd_setup, "_cleanup": cmd_cleanup}[sys.argv[1]](None)
    else:
        main()
