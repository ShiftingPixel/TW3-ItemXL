# Report and console

## The report

Every build writes `<game>\Mods\mod00000_ItemXL\itemxl_report.txt`. It starts with what was read, then lists what
needs attention.

```
ItemXL 1.0.0 build 2026-11-12 12:00:00  (1.5s)
TW3SE 1.3.3 (interface 25), game build 6adfbd7a

[import] modSomeOldMod: 12 records from whole-file overrides
[delta]  modMyMod: itemxl\my_mod.yaml (4 records)
[if]     modMyMod: itemxl\my_mod.yaml: item 'My Variant' skipped
[after]  modMyPatch after modSomeOldMod
[where]  modMyMod: item 'Every crossbow': 12 record(s): Crossbow 1, Crossbow 2, ...
[look]   modMyMod: item My Armor -> itemxl_my_armor
[file]   modMyMod: mymod\armor\my_armor.w2mesh
[tex]    2 texture(s) in mod00000_ItemXL\content\texture.cache
[text]   mod00000_ItemXL\tw3se\strings\en.txt
[script] mod00000_ItemXL\content\scripts\local\itemxl.ws

files written: 5
  ...
```

| Line | Meaning |
|---|---|
| `[import]` | a mod that replaces whole files, turned into changes ([more](08-load-order-and-compatibility.md#mods-that-replace-whole-files)) |
| `[delta]` | a YAML file and how many records it names |
| `[if]` | a file or record skipped because its [`$if`](06-rules.md#if) does not hold |
| `[after]` | an order set with [`$after`](06-rules.md#after) |
| `[where]` | what a [`$where`](06-rules.md#where) rule matched |
| `[look]`, `[file]`, `[tex]` | [custom appearances](07-appearance.md), their files and textures |
| `[text]`, `[script]` | texts for TW3SE and the script for the notices in the game |

Then the sections. A report with something in each looks like this:

```
CONFLICTS: 1
  item/Bodkin Bolt/price: modA = '5'  vs  modB = '7'  -> modB wins

ERRORS: 1
  modBroken: itemxl\broken.yaml: line 6: unexpected text after a value: :

WARNINGS: 2
  modMyMod: item 'My Sword': unknown attribute 'prise'
  modMyMod: item 'Every crossbow': $where matches no record

VANILLA GAME FILES: 1   (the game's own files, no mod causes these)
  gameplay\items\def_loot_quests.xml: dropped 1550 duplicate attribute(s), kept the first of each

stacked / overridden changes: 1
  modMyMod: item/Long Steel Sword: enhancement_slots also set by dlc\dlcx\data\gameplay\items\x.xml (on_conflict="extend"); ItemXL's value applies there too
```

What each section means:

| Section | Content | What to do |
|---|---|---|
| `CONFLICTS` | two mods set the same field differently; the winner is named | nothing, or decide with the load order or [`$after`](06-rules.md#after) |
| `ERRORS` | something could not be done: broken YAML (with line), a record that does not exist, a missing file, a mod named in `$requires` that is not installed | fix it; the rest of the build still happened |
| `WARNINGS` | probably a mistake: unknown attributes or stats, abilities, merchants, or ingredients that do not exist, new items missing a name or icon, a `$where` that matches nothing | check it |
| `VANILLA GAME FILES` | the game's own files have small faults (for example duplicate attributes); no mod causes these | nothing |
| `stacked / overridden changes` | changes that met another change or an XML override copy | for information |

## Notices in the game

About two seconds after a save is loaded, ItemXL shows a notice when:

- the last build had errors or YAML warnings, with their numbers;
- the save holds items the game no longer knows, because a mod was removed or an item renamed.

## The TW3SE console

TW3SE has its own console: press F2, open the **Console** tab. A line there is Lua; a line starting with `/` is a
command:

| Command | Answer |
|---|---|
| `/itemxl` | summary of the last build |
| `/itemxl version` | ItemXL and TW3SE versions |
| `/itemxl report` | the report from `CONFLICTS` on |
| `/itemxl show <name>` | a record as the game reads it now, every copy with its file (takes a few seconds) |

The game's own debug console (`~`) is a different console; ItemXL's commands are not available there.

Next: [Command-line tool](10-cli.md).
