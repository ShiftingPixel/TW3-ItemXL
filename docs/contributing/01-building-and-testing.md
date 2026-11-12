# Building and testing

## Requirements

- Windows. `build.bat` is a Windows batch file, the plugin is a Windows DLL, and the tests need an installed game.
- Python 3 with the packages `ziglang` (the C++ compiler), `pyyaml` and `jsonschema` (tests):

  ```powershell
  python -m pip install --user ziglang pyyaml jsonschema
  ```

- The TW3SE SDK, unpacked into `third_party\TW3SE-SDK` (not part of this repository; download it with TW3SE). Only
  the plugin needs its header; the tests use its CR2W and texture tools.
- An installed Witcher 3 for the tests. The tests look for it in the Steam and GOG install locations. If yours is
  elsewhere, set the environment variable `ITEMXL_GAME` to the game folder, or create a file `local_paths.json` in
  the repository root with this content:

  ```json
  {"GAME": "D:/Games/The Witcher 3"}
  ```

  `.gitignore` excludes this file, so your path is not committed by accident.

## Build

```powershell
plugin\build.bat
```

This builds two programs from the same sources into `plugin\out\`:

| File | Use |
|---|---|
| `ItemXL.dll` | the TW3SE plugin |
| `itemxl-cli.exe` | the [command-line tool](../modding/10-cli.md), also used by the tests |
| `*.pdb` | debug information; keep the ones of every released build to read crash reports |

The version is set only in `plugin\src\version.hpp`.

## Test

```powershell
python tests\run_all.py --build
```

`--build` runs `build.bat` first. A run takes about half a minute plus the build:

| Step | Checks |
|---|---|
| `yaml` | the C++ YAML reader against PyYAML (with ItemXL's tags) for every YAML file of the tests and examples, plus broken and hostile input |
| `browse` | `itemxl-cli search`, `show`, and `new` |
| `schema` | the VS Code schema accepts every valid test YAML and catches the typos of the `typos` case |
| `script` | the generated `itemxl.ws` and the notice for items that disappeared |
| `appearance` | the resource formats against the game's own files (templates, models, textures, icons) and a build of the look example |
| `case …` | every `tests\cases\*.json` (see below) |
| `output` | a rebuild replaces the output in place, removes stale files, and keeps unchanged output |
| `roundtrip` | every definition XML of the game read and written again; the written text reads back the same |

The tests work in `%TEMP%\itemxl-tests`.

Passing tests do not prove that the game reads a file as intended. Whatever touches how the game loads data needs
a check in the game as well; [Game behavior](05-game-behavior.md) lists what was confirmed there.

## Test cases

A case is a JSON file in `tests\cases\` plus a folder of the same name with its mods. `run_all.py` puts the mods
together in the work folder, builds them with `itemxl-cli build`, and checks the expectations.

```json
{
  "about": "what the case shows",
  "mods": {
    "modA": {"own": "Mods/modA"},
    "modB": {"project": "examples/modItemXLExampleBlade"},
    "modC": {"pack": "Mods/modC"},
    "modD": {"override": [{"file": "gameplay/items/def_item_bolts.xml", "item": "Bodkin Bolt", "attr": "price", "value": "7"}]}
  },
  "dlc": {"dlcx": {"pack": "dlc/dlcx"}},
  "settings": {"modA": [1, 0], "modB": [1, 1]},
  "expect": {
    "conflicts": 0,
    "errors": 0,
    "warnings": 0,
    "contains": [["gameplay/items/def_item_bolts.xml", "price=\"7\""]],
    "absent": [["gameplay/items/def_item_bolts.xml", "price=\"9\""]],
    "report_contains": ["[where]  modA: item 'Every bolt'"]
  }
}
```

Sources of a mod or DLC folder:

| Source | Meaning |
|---|---|
| `own` | a folder of the case |
| `project` | a folder of this repository |
| `pack` | loose files of the case, packed into `content\blob0.bundle` |
| `override` | a mod that replaces whole game files, made at test time from the game's own files with one attribute changed (game files are not shipped) |

`settings` holds `[Enabled, Priority]` per mod. Expectations: `conflicts`, `errors`, `warnings` (counts; warnings
about the game's own files are not counted), `errors_contain_cpp`, `warnings_contain`, `report_contains`, `contains`
and `absent` (text in a written file), `written` and `not_written` (files), `texts` and `absent_texts` (language,
text).

The case `examples` builds all example mods together and checks what they show; it covers the everyday features in
one build. Every new feature gets either a check there (when an example shows it) or a small case of its own (edge
cases, conflicts, errors). Keep cases small: the fewest records that show the feature. Each step prints its time; a
whole run should stay well under a minute.

Next: [Architecture](02-architecture.md).
