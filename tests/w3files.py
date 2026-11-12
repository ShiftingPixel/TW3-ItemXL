"""Game files for the tests, through itemxl-cli: bundles read and written, files taken from the game's bundles."""
import os
import shutil
import subprocess
import tempfile

from tools import paths


def _cli(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run([paths.CLI, *args], capture_output=True, text=True, encoding="utf-8", errors="replace")


def _read_tree(root: str) -> dict:
    out = {}
    for r, _d, fs in os.walk(root):
        for f in fs:
            fp = os.path.join(r, f)
            out[os.path.relpath(fp, root).replace("/", "\\").lower()] = open(fp, "rb").read()
    return out


def unpack(bundle: str) -> dict:
    """Every file of a bundle: lower depot path -> bytes."""
    tmp = tempfile.mkdtemp(prefix="itemxl-unpack-")
    try:
        r = _cli("unpack", bundle, tmp)
        if r.returncode != 0:
            raise RuntimeError(f"unpack {bundle}: {r.stderr.strip()}")
        return _read_tree(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def pack(files: dict, content_dir: str) -> None:
    """Loose files (depot path -> bytes) as content_dir\\blob0.bundle and metadata.store."""
    tmp = tempfile.mkdtemp(prefix="itemxl-pack-")
    try:
        for name, data in files.items():
            fp = os.path.join(tmp, name)
            os.makedirs(os.path.dirname(fp), exist_ok=True)
            with open(fp, "wb") as f:
                f.write(data)
        r = _cli("pack", tmp, content_dir)
        if r.returncode != 0:
            raise RuntimeError(f"pack: {r.stderr.strip()}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def extract(*depot_paths: str) -> dict:
    """Files of the game's own bundles (the first copy, base game before DLC): lower depot path -> bytes.
    Paths the game does not have are missing from the result."""
    tmp = tempfile.mkdtemp(prefix="itemxl-extract-")
    try:
        _cli("extract", tmp, *depot_paths, "--game", paths.GAME)
        return _read_tree(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
