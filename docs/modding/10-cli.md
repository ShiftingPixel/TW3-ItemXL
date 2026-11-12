# Command-line tool

`itemxl-cli.exe` is the same core as `ItemXL.dll`, as a program for the terminal. It reads the game's files without
starting the game. Run it from PowerShell or the Command Prompt; double-clicking it only flashes a window.

In PowerShell, start it from its own folder with `.\itemxl-cli`. The game folder is found through Steam or GOG, or
when the tool is somewhere inside the game folder; if not, add `--game "<game folder>"` to any command.

## search

```powershell
itemxl-cli search Bodkin
itemxl-cli search "Scherben-Schwert" --lang de
itemxl-cli search _store_ --tag loot
```

Records whose name or in-game name contains the text, with kind, in-game name, category, and file. `--tag` limits
the kind, `--lang` the language of the in-game names.

## show

```powershell
itemxl-cli show "Bodkin Bolt"
itemxl-cli show "bodkin_bolt _Stats" --tag ability
```

Every copy of a record as the game files have it, each with its file. This is the quickest way to see field names
and current values before writing a change.

## new

```powershell
itemxl-cli new "My Steel Sword" --base "Long Steel Sword"
itemxl-cli new "My Steel Sword" --base "Long Steel Sword" --out Mods\modMySword\itemxl\my_sword.yaml
```

A YAML file to start from: `$base`, `$text`, the base's main values as comments, and a `$sell` example. Without
`--out` it is printed; an existing file is never overwritten.

## schema

```powershell
itemxl-cli schema --vscode "C:\path\to\my mod"
```

Writes the schema for VS Code and the settings that use it; see [VS Code](11-vscode.md).

## build

```powershell
itemxl-cli build --mods "C:\path\to\Mods" --out "C:\path\to\test-output" --force
```

The same build the game does at start, into a folder of your choice. Use it to check a mod without starting the
game: the report is in the output folder. `--settings` points to a `mods.settings`, `--force` builds even when
nothing changed.

## import

```powershell
itemxl-cli import "C:\path\to\Mods\modOld\content\blob0.bundle"
```

Shows what ItemXL makes of a mod that replaces whole definition files: the changes it found, as JSON.

JSON is valid YAML, so this converts such a mod into an ItemXL mod in one step:

```powershell
itemxl-cli import "C:\path\to\Mods\modOld\content\blob0.bundle" | Set-Content -Encoding utf8 "C:\path\to\Mods\modNew\itemxl\converted.yaml"
```

(In PowerShell, write the file with `Set-Content -Encoding utf8` as above; a plain `>` writes UTF-16.) The result
works as it is, written in JSON's compact style on one line. Only the definition files are converted; scripts, models, and other files of the old mod are not.
Records the old mod added in files of its own (not replacing a game file) are not part of the output either.

## retarget

```powershell
itemxl-cli retarget in.w2ent out.w2ent "old\path\model.w2mesh=mymod\armor\my_armor.w2mesh"
```

A copy of a resource whose references point elsewhere, also inside an entity template's compiled data. Use it to make
your own template from one of the game's for [`$appearance`](07-appearance.md#way-2-your-own-template).

## Commands for tests

`yaml`, `roundtrip`, `cr2w`, `xbm`, `reddlc`, `unpack`, `pack`, and `extract` exist for the test suite; see
[Building and testing](../contributing/01-building-and-testing.md).

Next: [VS Code](11-vscode.md).
