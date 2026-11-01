#!/usr/bin/env python3
"""Fetch or verify the pinned sources for the isolated headless Vulkan CTS build."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

CTS_REVISION = "5a9cb32498158eb60774cf8a3d4e0b8c68f7fe9e"
DEPS = {
    "spirv-tools": ("KhronosGroup/SPIRV-Tools", "28f99f74a2a922ff85fc8fd966d6b3d6a45be795"),
    "glslang": ("KhronosGroup/glslang", "e7917c4ff6c660aba47b2df0c84f123d5bb1c3ce"),
    "spirv-headers": ("KhronosGroup/SPIRV-Headers", "fef168c54ed2e37c314eb251806aaf9877f38465"),
    "amber": ("google/amber", "c9f1e2871ccbb0b0564c4a33e359801144f6b7c1"),
    "jsoncpp": ("open-source-parsers/jsoncpp", "89e2973c754a9c02a49974d839779b151e95afd6"),
    "vulkansc-pcutil": ("KhronosGroup/VulkanSC-pcutil", "9723c1288a33f902a8129eda8f3f7577d4716b00"),
}
PNG_SHA256 = "71158e53cfdf2877bc99bcab33641d78df3f48e6e0daad030afe9cb8c031aa46"
PNG_URL = "https://codeload.github.com/pnggroup/libpng/tar.gz/refs/tags/v1.6.50"


def require_idle(build):
    for process in Path("/proc").iterdir():
        if not process.name.isdigit():
            continue
        try:
            argv = (process / "cmdline").read_bytes().split(b"\0")
            if Path(os.fsdecode(argv[0])).name not in ("ninja", "ninja-build"):
                continue
            if (process / "cwd").resolve() == build.resolve():
                raise RuntimeError(f"Build is active in {build} (Ninja PID {process.name}); refusing another writer")
        except (FileNotFoundError, PermissionError, ProcessLookupError):
            continue


def git_output(target, *args):
    return subprocess.run(["git", "-C", str(target), *args], text=True, capture_output=True)


def fetch_repo(target, repository, revision, log_path, verify_only):
    current = git_output(target, "rev-parse", "HEAD")
    if current.returncode == 0:
        if current.stdout.strip() != revision:
            raise RuntimeError(f"Unexpected revision in {target}; preserve it and choose a fresh CTS directory")
        status = git_output(target, "status", "--porcelain", "--untracked-files=no")
        if status.returncode or status.stdout:
            raise RuntimeError(f"Tracked changes in {target}; refusing to overwrite them")
        return revision
    if verify_only:
        raise RuntimeError(f"Missing checkout: {target}")
    if target.exists() and any(target.iterdir()) and not (target / ".git").is_dir():
        raise RuntimeError(f"Nonempty directory without a Git checkout: {target}")
    target.mkdir(parents=True, exist_ok=True)
    url = f"git@github.com:{repository}.git"
    with log_path.open("a") as log:
        def run(*args):
            cmd = ["git", "-C", str(target), *args]
            log.write("$ " + shlex.join(cmd) + "\n")
            log.flush()
            subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, check=True)
        if not (target / ".git").is_dir():
            run("init")
            run("remote", "add", "origin", url)
        elif git_output(target, "remote", "get-url", "origin").stdout.strip() != url:
            raise RuntimeError(f"Unexpected origin in incomplete checkout {target}")
        run("fetch", "--depth", "1", "origin", revision)
        run("checkout", "--detach", "FETCH_HEAD")
    actual = git_output(target, "rev-parse", "HEAD").stdout.strip()
    if actual != revision:
        raise RuntimeError(f"Revision verification failed in {target}")
    return actual


def verify_libpng(archive, target):
    with tarfile.open(archive) as package:
        for member in package:
            if member.isdir():
                continue
            relative = Path(*Path(member.name).parts[1:])
            if not member.isfile() or relative.is_absolute() or ".." in relative.parts:
                raise RuntimeError(f"Unexpected libpng archive member: {member.name}")
            actual = target / relative
            if actual.is_symlink() or not actual.is_file():
                raise RuntimeError(f"Missing or unexpected libpng source: {actual}")
            if actual.read_bytes() != package.extractfile(member).read():
                raise RuntimeError(f"Modified libpng source: {actual}")
    if (target / "pnglibconf.h").read_bytes() != (target / "scripts/pnglibconf.h.prebuilt").read_bytes():
        raise RuntimeError(f"Unexpected libpng configuration: {target / 'pnglibconf.h'}")


def prepare(source, build, verify_only):
    if not verify_only:
        require_idle(build)
        build.mkdir(parents=True, exist_ok=True)
    fetch_repo(source, "KhronosGroup/VK-GL-CTS", CTS_REVISION,
               build / "preparation.log", verify_only)
    pins = (source / "external/fetch_sources.py").read_text()

    def dependency(item):
        name, (repository, revision) = item
        if revision not in pins:
            raise RuntimeError(f"Dependency pin no longer matches CTS: {name}")
        actual = fetch_repo(source / "external" / name / "src", repository,
                            revision, build / f"fetch-{name}.log", verify_only)
        print(f"{name}: {actual}", flush=True)
        return name, {"repository": repository, "revision": actual}

    with ThreadPoolExecutor(max_workers=2) as pool:
        manifest = dict(pool.map(dependency, DEPS.items()))
    archive = build / "archives/libpng-1.6.50.tar.gz"
    if PNG_SHA256 not in pins:
        raise RuntimeError("libpng checksum no longer matches CTS")
    if not archive.exists():
        if verify_only:
            raise RuntimeError(f"Missing verified archive: {archive}")
        archive.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(prefix="libpng-", suffix=".part", dir=archive.parent, delete=False) as output:
            temporary = Path(output.name)
            print(f"Downloading libpng to {temporary}", flush=True)
            with urllib.request.urlopen(PNG_URL, timeout=90) as response:
                shutil.copyfileobj(response, output)
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != PNG_SHA256:
            raise RuntimeError(f"Checksum mismatch; preserved download at {temporary}")
        temporary.rename(archive)
    actual = hashlib.sha256(archive.read_bytes()).hexdigest()
    if actual != PNG_SHA256:
        raise RuntimeError(f"Checksum mismatch; preserved archive at {archive}")
    target = source / "external/libpng/src"
    if not target.exists():
        if verify_only:
            raise RuntimeError(f"Missing libpng source: {target}")
        stage = Path(tempfile.mkdtemp(prefix="extract-libpng-", dir=build))
        with tarfile.open(archive) as package:
            package.extractall(stage, filter="data")
        roots = list(stage.iterdir())
        if len(roots) != 1 or not roots[0].is_dir():
            raise RuntimeError(f"Unexpected archive layout in {stage}")
        shutil.move(str(roots[0]), target)
        stage.rmdir()
        shutil.copyfile(target / "scripts/pnglibconf.h.prebuilt", target / "pnglibconf.h")
    if not (target / "pnglibconf.h").is_file():
        raise RuntimeError(f"Incomplete libpng source: {target}")
    verify_libpng(archive, target)
    manifest["libpng"] = {"version": "1.6.50", "archive_sha256": actual}
    manifest["zlib"] = {"source": "existing read-only target sysroot"}
    manifest["omitted"] = ["vulkan-validationlayers", "vulkan-docs (checked-in generated framework)",
                           "renderdoc", "vulkan-video-samples", "video_generator"]
    if not verify_only:
        (build / "DEPENDENCIES.json").write_text(json.dumps(manifest, indent=2) + "\n")
        (build / "CTS_REVISION").write_text(CTS_REVISION + "\n")
    print(f"Verified CTS {CTS_REVISION}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--verify-only", action="store_true", help="Read-only pin/checksum verification")
    args = parser.parse_args()
    try:
        prepare(args.source.resolve(), args.build.resolve(), args.verify_only)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
