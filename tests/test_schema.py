"""The VS Code schema (itemxl-cli schema): every YAML of the tests and examples that builds without warnings has to
pass it, and each typo of the typos case has to be caught by it."""
import glob
import json
import os
import subprocess
import sys

import jsonschema
import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from yamlref import load_all, plain  # noqa: E402
from tools import paths  # noqa: E402

# the typos case: what the schema has to find (a part of the path of each error)
TYPOS = ["SlashingDamge", "prise", "crafter", "Steel ingott"]


def leaves(e):
    """The errors inside an anyOf (record or null, name or $(key)): the ones that say what is wrong."""
    if not e.context:
        skip = "is not of type 'null'" in e.message or e.validator == "pattern"
        return [] if skip else [e]
    out = []
    for c in e.context:
        out += leaves(c)
    return out or [e]


def new_records(paths_):
    """The names of the records the YAMLs create ($base, $xml): the schema only knows the game's records, so a new
    record used as a name elsewhere (an ingredient, a base ability) is underlined in VS Code but works."""
    out = set()
    for path in paths_:
        try:
            with open(path, encoding="utf-8-sig") as fh:
                docs = load_all(fh)
        except yaml.YAMLError:
            continue
        for d in docs:
            for recs in (plain(d) or {}).values():
                if isinstance(recs, dict):
                    out |= {n for n, v in recs.items() if isinstance(v, dict) and ("$base" in v or "$xml" in v)}
    return out


def errors(validator, path, made=frozenset()):
    out = []
    with open(path, encoding="utf-8-sig") as fh:
        try:
            docs = load_all(fh)
        except yaml.YAMLError:
            return None                      # broken on purpose (the broken case)
    for d in docs:
        if not d:
            continue
        for e in validator.iter_errors(plain(d)):
            for leaf in leaves(e):
                if leaf.validator == "enum" and leaf.instance in made:
                    continue
                out.append("/".join(str(p) for p in leaf.absolute_path) + ": " + leaf.message[:160])
    return out


def run(verbose=False):
    out = os.path.join(paths.WORK, "itemxl.schema.json")
    os.makedirs(paths.WORK, exist_ok=True)
    r = subprocess.run([paths.CLI, "schema", "--game", paths.GAME, "--out", out], capture_output=True, text=True)
    if r.returncode:
        return ["schema: " + r.stderr + r.stdout]
    with open(out, encoding="utf-8") as fh:
        validator = jsonschema.Draft7Validator(json.load(fh))
    problems = []
    files = sorted(glob.glob(os.path.join(HERE, "cases", "**", "*.yaml"), recursive=True) +
                   glob.glob(os.path.join(HERE, "..", "examples", "**", "*.yaml"), recursive=True))
    made = new_records(files)
    for f in files:
        rel = os.path.relpath(f, os.path.join(HERE, "..")).replace("\\", "/")
        errs = errors(validator, f, made)
        if errs is None:
            continue
        if "/typos/" in rel:
            for t in TYPOS:
                if not any(t in e for e in errs):
                    problems.append(f"{rel}: the schema misses the typo {t}")
            if verbose:
                print("\n".join("  caught " + e for e in errs))
        elif errs:
            problems += [f"{rel}: {e}" for e in errs]
        elif verbose:
            print("ok", rel)
    return problems


def browse():
    """show and search: a German in-game name finds the record, show prints both copies (items and items_plus)."""
    problems = []
    cli = lambda *a: subprocess.run([paths.CLI, *a, "--game", paths.GAME], capture_output=True, text=True,
                                    encoding="utf-8").stdout
    found = cli("search", "Scherben-Schwert", "--tag", "item", "--lang", "de")
    if "Novigraadan sword 4_crafted" not in found:
        problems.append("search by the German name does not find Novigraadan sword 4_crafted: " + found[-500:])
    shown = cli("show", "Novigraadan sword 4_crafted", "--tag", "item")
    for f in (r"gameplay\items\def_item_crafting_weapons.xml", r"gameplay\items_plus\def_item_crafting_weapons.xml"):
        if f not in shown:
            problems.append(f"show does not list {f}")
    if "no record" not in cli("show", "No Such Item 123"):
        problems.append("show of a missing record says nothing")
    # new: a YAML file that reads back and names its base
    made = cli("new", "My Test Sword", "--base", "Novigraadan sword 4_crafted")
    try:
        docs = load_all(made)
        spec = docs[0]["item"]["My Test Sword"]
        if spec.get("$base") != "Novigraadan sword 4_crafted" or "$text" not in spec:
            problems.append("new: the YAML lacks $base or $text: " + made[:300])
    except Exception as ex:
        problems.append(f"new: the YAML does not read back ({ex}): " + made[:300])
    return problems


if __name__ == "__main__":
    p = browse() + run(verbose=True)
    print("\n".join(p))
    sys.exit(1 if p else 0)
