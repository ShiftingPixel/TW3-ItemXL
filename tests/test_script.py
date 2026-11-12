"""The script part (content\\scripts\\local\\itemxl.ws) and the warning for new items that are gone.

Builds twice with itemxl-cli into one output folder: first with the example blade, then without it. The second
report has to warn that the blade is gone; the script has to carry the build problems (here: none, then none)."""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
from tools import paths  # noqa: E402

ROOT = os.path.join(HERE, "..")


def build(mods, out):
    r = subprocess.run([paths.CLI, "build", "--game", paths.GAME, "--mods", mods, "--out", out, "--force"],
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    report = open(os.path.join(out, "itemxl_report.txt"), encoding="utf-8").read() \
        if os.path.isfile(os.path.join(out, "itemxl_report.txt")) else r.stdout + r.stderr
    return r.returncode, report


def run():
    problems = []
    w = os.path.join(paths.WORK, "script")
    shutil.rmtree(w, ignore_errors=True)
    mods, out = os.path.join(w, "Mods"), os.path.join(w, "out")
    os.makedirs(mods)
    shutil.copytree(os.path.join(ROOT, "examples", "modItemXLExampleBlade"), os.path.join(mods, "modItemXLExampleBlade"))
    code, rep = build(mods, out)
    if code:
        return ["first build failed: " + rep[-500:]]
    ws_path = os.path.join(out, "content", "scripts", "local", "itemxl.ws")
    if not os.path.isfile(ws_path):
        return ["no content\\scripts\\local\\itemxl.ws"]
    ws = open(ws_path, encoding="utf-8").read()
    for need in ("@wrapMethod(CR4Player)", "saved var itemxlKnownItems", "timer function ItemXLAfterLoad",
                 "function ItemXL_BuildId()"):
        if need not in ws:
            problems.append(f"itemxl.ws lacks {need}")
    if 'return "";' not in ws:
        problems.append("a build without problems should have an empty problem text")
    items = open(os.path.join(out, "itemxl.items"), encoding="utf-8").read()
    if "ItemXL Example Blade\n" not in items or "ItemXL Example Blade schematic\n" not in items:
        problems.append("itemxl.items lacks the blade: " + items)
    # second build: the blade's mod is gone, and a broken YAML gives an error
    shutil.rmtree(os.path.join(mods, "modItemXLExampleBlade"))
    os.makedirs(os.path.join(mods, "modBroken", "itemxl"))
    with open(os.path.join(mods, "modBroken", "itemxl", "x.yaml"), "w", encoding="utf-8") as f:
        f.write("item:\n  A: {price: 1\n")
    code, rep = build(mods, out)
    for n in ("ItemXL Example Blade", "ItemXL Example Blade schematic"):
        if f"item '{n}' was made by the last build and is gone now" not in rep:
            problems.append(f"the report does not say that '{n}' is gone")
    ws2 = open(ws_path, encoding="utf-8").read()
    if "1 error(s)" not in ws2 or "1 Fehler" not in ws2:
        problems.append("itemxl.ws does not carry the build error")
    if ws2.split("ItemXL_BuildId() : string { return ")[1][:18] == ws.split("ItemXL_BuildId() : string { return ")[1][:18]:
        problems.append("the build id did not change")
    return problems


if __name__ == "__main__":
    p = run()
    print("\n".join(p) or "ok")
    sys.exit(1 if p else 0)
