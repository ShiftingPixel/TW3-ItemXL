r"""All ItemXL tests in one run.

    python tests/run_all.py            uses plugin/out/itemxl-cli.exe as it is
    python tests/run_all.py --build    builds the plugin and the CLI first (plugin/build.bat)

Steps:
  1. YAML      the C++ reader against PyYAML, every YAML file of the tests
  2. browse, schema, script, appearance   see tests/test_*.py
  3. cases     every tests/cases/*.json: the mods are put together in the work folder (tools/paths.py: WORK), built
               by itemxl-cli, and checked against the case's expectations (report and written files)
  4. roundtrip every definition XML of the game read and written again; the written text must read back the same

Everything a case needs is in the project or made from the game's own files. The sources of a mod or DLC folder:
  {"own": "Mods/modX"}        a folder of the case (tests/cases/<case>/...)
  {"project": "examples/..."} a folder of the project
  {"pack": "dlc/dlcx"}        loose files of the case, packed into content\blob0.bundle (a DLC folder of a mod)
  {"override": [...]}         a mod that replaces whole files of the game: each entry names a file of the game, an
                              item in it and the attribute to set; packed into content\blob0.bundle
"""
import glob
import json
import os
import shutil
import stat
import subprocess
import sys
import time
import re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)

from tools import paths  # noqa: E402
import w3files  # noqa: E402
import test_schema  # noqa: E402
import test_script  # noqa: E402
import test_appearance  # noqa: E402
import test_yaml  # noqa: E402

WORK = paths.WORK


def rmtree(p: str) -> None:
    """Removes a folder, also read-only files (copies can keep that flag)."""
    def unlock(func, path, _exc):
        os.chmod(path, stat.S_IWRITE)
        func(path)
    if os.path.isdir(p):
        shutil.rmtree(p, onexc=unlock)
results: list[tuple[str, list[str], str]] = []    # (step, problems, note)
_last = [time.time()]


def step(name: str, problems: list[str], note: str = "") -> None:
    results.append((name, problems, note))
    now = time.time()
    took, _last[0] = now - _last[0], now
    print(f"{'ok  ' if not problems else 'FAIL'} {name}" + (f"  ({note})" if note else "") + f"  [{took:.1f}s]")
    for p in problems:
        print("     " + p.replace("\n", "\n     "))


# ----------------------------------------------------------------- cases ----

def fetch(spec: dict, case_dir: str, target: str) -> str | None:
    """Copies one mod or DLC folder into target. Returns a problem text or None."""
    if "own" in spec:
        src = os.path.join(case_dir, spec["own"])
    elif "project" in spec:
        src = os.path.join(ROOT, spec["project"])
    elif "pack" in spec:
        src = os.path.join(case_dir, spec["pack"])
        if not os.path.isdir(src):
            return f"not found: {src}"
        files = {}
        for r, _d, fs in os.walk(src):
            for f in fs:
                fp = os.path.join(r, f)
                files[os.path.relpath(fp, src)] = open(fp, "rb").read()
        w3files.pack(files, os.path.join(target, "content"))
        return None
    elif "override" in spec:
        files = {}
        game = w3files.extract(*{o["file"] for o in spec["override"]})
        for o in spec["override"]:
            key = o["file"].lower()
            if key not in game:
                return f"the game has no {o['file']}"
            text = files.get(o["file"]) or game[key].decode("utf-8")
            if "drop_item" in o:
                # Simulate an outdated mod built against an older game patch that lacks a record added later
                pat = r'<item\s+name\s*=\s*"' + re.escape(o["drop_item"]) + r'"[^>]*>.*?</item>\s*'
                if not re.search(pat, text, flags=re.S):
                    return f"{o['file']}: no item {o['drop_item']} to drop"
                files[o["file"]] = re.sub(pat, "", text, count=1, flags=re.S)
                continue
            # (the game's files pad "=" with tabs)
            tag = re.search(r'<item\s+name\s*=\s*"' + re.escape(o["item"]) + r'"[^>]*>', text)
            attr = r'\b' + o["attr"] + r'\s*=\s*"[^"]*"'
            if not tag or not re.search(attr, tag.group(0)):
                return f"{o['file']}: no item {o['item']} with {o['attr']}"
            new = re.sub(attr, f'{o["attr"]}="{o["value"]}"', tag.group(0), count=1)
            files[o["file"]] = text.replace(tag.group(0), new, 1)
        enc = spec.get("encoding", "utf-8")
        w3files.pack({k: v.encode(enc) for k, v in files.items()}, os.path.join(target, "content"))
        return None
    else:
        return f"unknown source {spec}"
    if not os.path.isdir(src):
        return f"not found: {src}"
    shutil.copytree(src, target)
    return None


def run_case(path: str) -> None:
    name = os.path.splitext(os.path.basename(path))[0]
    with open(path, encoding="utf-8") as f:
        case = json.load(f)
    case_dir = os.path.join(HERE, "cases", name)
    w = os.path.join(WORK, name)
    rmtree(w)
    problems = []
    for kind, folder in (("mods", "Mods"), ("dlc", "dlc")):
        for mod, spec in case.get(kind, {}).items():
            p = fetch(spec, case_dir, os.path.join(w, folder, mod))
            if p:
                problems.append(p)
    if problems:
        step(f"case {name}", problems, "inputs missing")
        return
    settings = os.path.join(w, "mods.settings")
    with open(settings, "w", encoding="utf-8") as f:
        for mod, (enabled, prio) in case.get("settings", {}).items():
            f.write(f"[{mod}]\nEnabled={enabled}\nPriority={prio}\n\n")
    dlc = os.path.join(w, "dlc") if case.get("dlc") else ""

    cpp = os.path.join(w, "out")
    args = [paths.CLI, "build", "--game", paths.GAME, "--mods", os.path.join(w, "Mods"), "--settings", settings,
            "--out", cpp, "--force"]
    if dlc:
        args += ["--dlc", dlc]
    r = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    rep_path = os.path.join(cpp, "itemxl_report.txt")
    rep = open(rep_path, encoding="utf-8").read() if os.path.isfile(rep_path) else ""
    if not rep:
        step(f"case {name}", [f"itemxl-cli wrote no report: {r.stderr[-500:]}"])
        return
    sections = report_sections(rep)
    conflicts, errors = sections.get("CONFLICTS", []), sections.get("ERRORS", [])
    # the warnings of the mods; the ones about the game's own files depend on the installed game version
    warnings = [x for x in sections.get("WARNINGS", []) if not x.startswith("vanilla ") and "duplicate attribute" not in x]

    exp = case.get("expect", {})
    if "conflicts" in exp and len(conflicts) != exp["conflicts"]:
        problems.append(f"conflicts: {conflicts}, expected {exp['conflicts']}")
    if "errors" in exp:
        if len(errors) != exp["errors"]:
            problems.append(f"errors: {errors}, expected {exp['errors']}")
    elif errors:
        problems.append(f"errors in the build: {errors}")
    for e_ in exp.get("errors_contain_cpp", []):
        if e_.replace("/", chr(92)) not in rep:
            problems.append(f"the report lacks the error: {e_}")
    if "warnings" in exp and len(warnings) != exp["warnings"]:
        problems.append(f"warnings: {warnings}, expected {exp['warnings']}")
    for t_ in exp.get("report_contains", []):
        if t_.replace("/", chr(92)) not in rep:
            problems.append(f"the report lacks: {t_}")
    for w_ in exp.get("warnings_contain", []):
        if w_ not in warnings and w_ not in [x.replace(chr(92), "/") for x in warnings]:
            problems.append(f"missing warning: {w_}")
    b = os.path.join(cpp, "content", "blob0.bundle")
    out = {k: v.decode("utf-8-sig", "replace") for k, v in w3files.unpack(b).items()} if os.path.isfile(b) else {}

    def key(f):                       # cases write depot paths with / for readability
        return f.replace("/", chr(92)).lower()
    for f, text in exp.get("contains", []):
        if text not in out.get(key(f), ""):
            problems.append(f"{f} should contain {text}")
    for f, text in exp.get("absent", []):
        if text in out.get(key(f), ""):
            problems.append(f"{f} should not contain {text}")
    for lang, text in exp.get("texts", []):
        tp = os.path.join(cpp, "tw3se", "strings", lang + ".txt")
        body = open(tp, encoding="utf-8").read() if os.path.isfile(tp) else ""
        if text not in body:
            problems.append(f"texts {lang} should contain {text!r}")
    for lang, text in exp.get("absent_texts", []):
        tp = os.path.join(cpp, "tw3se", "strings", lang + ".txt")
        if os.path.isfile(tp) and text in open(tp, encoding="utf-8").read():
            problems.append(f"texts {lang} should not contain {text!r}")
    for f in exp.get("written", []):
        if key(f) not in out:
            problems.append(f"{f} should be written")
    for f in exp.get("not_written", []):
        if key(f) in out:
            problems.append(f"{f} should not be written")
    step(f"case {name}", problems, f"{len(out)} files, {len(conflicts)} conflicts")


def report_sections(rep: str) -> dict:
    """The lists of the report: "CONFLICTS: n", "ERRORS: n", "WARNINGS: n", each followed by its indented lines."""
    out, cur = {}, None
    for line in rep.splitlines():
        m = re.match(r"([A-Z]+): \d+$", line)
        if m:
            cur = out.setdefault(m.group(1), [])
        elif cur is not None and line.startswith("  "):
            cur.append(line[2:])
        else:
            cur = None
    return out


# ------------------------------------------------------------- roundtrip ----

def roundtrip() -> None:
    out = os.path.join(WORK, "roundtrip")
    rmtree(out)
    r = subprocess.run([paths.CLI, "roundtrip", out, "--game", paths.GAME], capture_output=True, text=True)
    lines = r.stdout.strip().splitlines()
    problems = [] if r.returncode == 0 else lines[-20:]
    step("roundtrip", problems, lines[-1] if lines else "no output")


def output() -> None:
    """The output folder: a rebuild replaces it file by file and removes what an older build left; with nothing
    changed the CLI keeps it as it is."""
    w = os.path.join(WORK, "bolts")
    out = os.path.join(w, "out")
    problems = []
    if not os.path.isdir(out):
        step("output", ["the bolts case did not run"])
        return
    stale = os.path.join(out, "content", "old_build_leftover.bundle")
    open(stale, "wb").write(b"x")
    args = [paths.CLI, "build", "--game", paths.GAME, "--mods", os.path.join(w, "Mods"), "--settings",
            os.path.join(w, "mods.settings"), "--out", out]
    subprocess.run(args + ["--force"], capture_output=True)
    if os.path.exists(stale):
        problems.append("a file of an older build was not removed")
    if os.path.isdir(out + ".tmp"):
        problems.append("the staging folder was left behind")
    r = subprocess.run(args, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if "nothing changed" not in (r.stdout + r.stderr):
        problems.append("a second build without changes did not keep the output: " + (r.stdout + r.stderr)[-300:])
    step("output", problems, "rebuild in place, stale files removed, unchanged inputs kept")


def main() -> int:
    t0 = time.time()
    if "--build" in sys.argv:
        r = subprocess.run(["cmd", "/c", os.path.join(ROOT, "plugin", "build.bat")], capture_output=True, text=True)
        step("build", [] if r.returncode == 0 else [r.stdout[-1500:] + r.stderr[-1500:]])
    if not os.path.isfile(paths.CLI):
        step("cli", [f"{paths.CLI} is missing: run with --build"])
        return 1
    os.makedirs(WORK, exist_ok=True)
    step("yaml", test_yaml.run(), f"{len(test_yaml.files())} files")
    step("browse", test_schema.browse(), "show and search")
    step("schema", test_schema.run(), "VS Code schema against the test YAMLs")
    step("script", test_script.run(), "itemxl.ws, and new items that are gone")
    step("appearance", test_appearance.run(), "$appearance: CR2W, .xbm, texture.cache, templates, buffers0")
    for c in sorted(glob.glob(os.path.join(HERE, "cases", "*.json"))):
        run_case(c)
    output()
    roundtrip()
    bad = sum(1 for _n, p, _x in results if p)
    print(f"\n{len(results) - bad} of {len(results)} steps ok ({time.time() - t0:.0f}s)")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
