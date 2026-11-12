"""Compares the C++ YAML reader (itemxl-cli yaml) with PyYAML (tests/yamlref.py) for every YAML file of the tests
(tests/yaml/ and tests/cases/)."""
import glob
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from tools import paths  # noqa: E402
from yamlref import load_all, plain  # noqa: E402


def files() -> list[str]:
    out = []
    for pat in ("yaml/**/*.yaml", "yaml/**/*.yml", "cases/**/*.yaml", "cases/**/*.yml",
                "../examples/**/*.yaml"):
        out += glob.glob(os.path.join(HERE, pat), recursive=True)
    return sorted(out)


def run(verbose: bool = False) -> list[str]:
    diffs = []
    for f in files():
        with open(f, encoding="utf-8-sig") as fh:
            try:
                want = plain(load_all(fh))
            except Exception:                # a broken file: the C++ reader has to refuse it as well
                want = "ERROR"
        out = subprocess.run([paths.CLI, "yaml", f], capture_output=True, text=True, encoding="utf-8").stdout
        try:
            got = json.loads(out)
        except json.JSONDecodeError:
            got = "ERROR" if out.startswith("ERROR") else out
        rel = os.path.relpath(f, HERE)
        if got != want:
            diffs.append(f"{rel}:\n  py : {json.dumps(want, ensure_ascii=False)}\n  cpp: {got}")
        elif verbose:
            print("same", rel)

    # Edge-case hardening: huge integers fall back to strings without throwing, invalid hex escapes and
    # alias-bomb / deep nesting fail cleanly with ERROR instead of crashing or hanging.
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        def check_raw(name: str, body: str, expect_err: bool, expect_substr: str = ""):
            p = os.path.join(tmp, name)
            open(p, "w", encoding="utf-8").write(body)
            out = subprocess.run([paths.CLI, "yaml", p], capture_output=True, text=True, encoding="utf-8").stdout
            if expect_err:
                if not out.startswith("ERROR") or (expect_substr and expect_substr not in out):
                    diffs.append(f"{name}: expected ERROR {expect_substr!r}, got {out!r}")
            else:
                if out.startswith("ERROR") or (expect_substr and expect_substr not in out):
                    diffs.append(f"{name}: expected {expect_substr!r}, got {out!r}")

        check_raw("huge_int.yaml", "a: 999999999999999999999999999999\n", False, '"999999999999999999999999999999"')
        check_raw("bad_hex.yaml", 'a: "\\xZZ"\n', True, "invalid hex escape")
        check_raw("trailing_bs.yaml", 'a: "\\', True, "unclosed quote")
        check_raw("deep_flow.yaml", "a: " + "[" * 80 + "1" + "]" * 80 + "\n", True, "nesting too deep")
        bomb = "a0: &a0 [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]\n"
        for i in range(1, 8):
            bomb += f"a{i}: &a{i} [*a{i-1}, *a{i-1}, *a{i-1}, *a{i-1}, *a{i-1}, *a{i-1}]\n"
        check_raw("bomb.yaml", bomb, True, "alias expansion limit exceeded")

    return diffs


if __name__ == "__main__":
    d = run(verbose=True)
    for x in d:
        print("DIFF", x)
    sys.exit(1 if d else 0)
