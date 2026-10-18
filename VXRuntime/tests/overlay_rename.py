#!/usr/bin/env python3
"""CRT rename must leave the physical original tree unchanged, including on failures."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

runtime, program = map(lambda p: str(Path(p).resolve()), sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="rename-", dir="build") as work:
    root = Path(work).resolve()
    tree, shadow = root / "tree", root / "shadow"
    tree.mkdir()
    shadow.mkdir()
    payload = bytes(range(256)) * 1025  # More than one copy buffer.
    (tree / "source").write_bytes(payload)
    (tree / "existing").write_bytes(b"keep")
    (tree / "dir").mkdir()
    (tree / "dir" / "child").write_bytes(b"child")
    (tree / "blocked-src").mkdir()
    (tree / "blocked-src" / "child").write_bytes(b"copy must fail")
    (tree / "empty").mkdir()
    (shadow / "blocked-src").write_bytes(b"block copy")
    (shadow / "blocked").write_bytes(b"block")
    (shadow / "existing-shadow").write_bytes(b"shadow")

    def snapshot(path):
        return {str(p.relative_to(path)): ("dir" if p.is_dir() else p.read_bytes())
                for p in path.rglob("*")}

    original = snapshot(tree)
    env = dict(os.environ, VXRT_OVERLAY=f"{tree}:{shadow}",
               VXRT_ROOT=str(root / "runtime"), VXRT_NO_ACTIVATE="1")

    def run(source, target, ok, wide=False):
        subprocess.run([runtime, program, "wrename" if wide else "rename",
                        "Z:" + str(source), "Z:" + str(target), "ok" if ok else "fail"],
                       env=env, check=True)
        assert snapshot(tree) == original, "rename changed the physical original tree"
        assert not list(shadow.rglob("*.rename.*")), "temporary copy leaked"

    run(tree / "source", tree / "new", True)
    assert (shadow / "new").read_bytes() == payload
    run(tree / "source", tree / "wide", True, wide=True)
    assert (shadow / "wide").read_bytes() == payload
    before = snapshot(shadow)
    for source, target in [("source", "existing"), ("source", "existing-shadow"),
                           ("source", "blocked/child"), ("dir", "moved-dir"),
                           ("missing", "missing-target"), ("source", "source"),
                           ("empty", "moved-empty"), ("blocked-src/child", "copy-failure")]:
        run(tree / source, tree / target, False)
        assert snapshot(shadow) == before, "failed rename changed overlay contents"
    run(tree / "source", root / "absent-parent" / "child", False)
    assert snapshot(shadow) == before, "failed publication left a copy behind"
    run(tree / "new", tree / "renamed", True)
    assert not (shadow / "new").exists()
    assert (shadow / "renamed").read_bytes() == payload
    run(tree / "source", root / "outside", True)
    assert (root / "outside").read_bytes() == payload
print("CRT overlay rename: original tree preserved; success/failure/wide/large-file checks passed")
