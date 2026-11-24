# Report and console

## The report

Every build writes `<game>\Mods\mod00000_ItemXL\itemxl_report.txt`. It starts with what was read, then lists what
needs attention.

```
ItemXL 1.1.0 build 2026-11-24 12:00:00  (0.7s)
TW3SE 1.4.2 (interface 29), game build 6adfbd7a

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

ItemXL shows these notices in the game, in English:

| Notice | When |
|---|---|
| `ItemXL: Items in this save no longer exist: <items>` | about two seconds after a save is loaded, when it holds items the game no longer knows (a mod was removed or an item renamed) |
| `ItemXL: Changes from <mods> could not be applied. See itemxl_report.txt` | about two seconds after a save is loaded, when the last build had errors; once per build and save. Warnings only go to the report |
| `ItemXL: <name> updated` or `ItemXL: <n> entries updated` | when a change was loaded while the game runs (see below) |

## The TW3SE console

TW3SE has its own console: press F2, open the **Console** tab. A line there is Lua; a line starting with `/` is a
command:

| Command | Answer |
|---|---|
| `/itemxl` | summary of the last build |
| `/itemxl version` | ItemXL and TW3SE versions |
| `/itemxl report` | the report from `CONFLICTS` on |
| `/itemxl show <name>` | a record as the game reads it now, every copy with its file (takes a few seconds) |
| `/itemxl reload` | build now and load what changed into the running game |
| `/itemxl live off` | stop watching for changes until the next start (`/itemxl live on` turns it on again) |

The game's own debug console (`~`) is a different console; ItemXL's commands are not available there.

## Changes while the game runs

With TW3SE 1.4.0 or later, ItemXL watches the mods' YAML files while the game runs. Save one, and about two
seconds later the changed records are in the game: ItemXL builds in the background, compares the result with what
the game loaded at the start, and loads only the records that differ. The game shows "ItemXL: ItemXL Example
Blade updated", or "ItemXL: 3 entries updated" when more changed; the TW3SE log names the records, and the report is
written again.

The watching is built so that it is never felt in the game, with any number of mods. It lists no folder while the
game runs: it watches exactly the YAML files that were there at the start and asks once a second for the size and
date of each, in a background thread of the lowest priority. A new YAML file or a new mod is seen at the next start
or with `/itemxl reload`, which looks through the folders once. Should a look ever take longer than 100 ms, the
watching stops for the session and the TW3SE log says so; `/itemxl reload` still works then.

Some changes wait for the next start, and the TW3SE log says so:

- items already in an inventory may keep what they had; to be sure, look at a new one (for example from
  `w3.inv.add`). Rune slots, for example, follow the game's own rule even after a restart: an item keeps the slots
  it was made with, so more slots reach every item, fewer only new ones
- records that were removed, appearances, models, textures and icons
- New Game Plus definitions (`items_plus`) change live only in a New Game Plus game; in another game they are left
  out, they are not needed there
- names and descriptions (`$text`) are written at once; whether TW3SE shows them before the next start is not
  certain

The next start builds the output again as usual, with all changes in it.

### For other mods

After each live change ItemXL sends a message on the TW3SE channel `itemxl.live`, one line per changed record:
the tag, a tab, the name. A Lua mod that keeps its own state about items can react to it:

```lua
w3.on_message("itemxl.live", function(text)
  for tag, name in text:gmatch("([^\t\n]+)\t([^\n]+)") do
    print("ItemXL changed " .. tag .. " " .. name)
  end
end)
```

C++ plugins listen with `RegisterOnMessage`, scripts with `TW3SE_Receive("itemxl.live")`.

Next: [Command-line tool](10-cli.md).
