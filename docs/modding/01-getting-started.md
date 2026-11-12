# Getting started

This page takes you from nothing to a working ItemXL mod. It assumes you can install Witcher 3 mods by hand and
have edited a text file before. No REDkit, no WolvenKit, and no XML editing are needed.

## What you need

- The Witcher 3 Remastered, game version 5.0 or later
- [TW3SE](https://www.nexusmods.com/witcher3/mods/13837), The Witcher 3 Script Extender
- `ItemXL.dll` in `<game>\bin\x64_dx12\tw3se\plugins\`
- A text editor. [VS Code](11-vscode.md) is recommended: ItemXL gives it a schema with completion and checks.
- Optional: [`itemxl-cli.exe`](10-cli.md) to look up records and check a mod without starting the game

Players of your mod need the same: TW3SE, ItemXL, and your mod folder.

## 1. Make the mod folder

An ItemXL mod is an ordinary Witcher 3 mod folder with an `itemxl` folder inside:

```
<game>\Mods\
  modMyFirstItemXL\
    itemxl\
      my_first.yaml
```

The folder name follows the usual Witcher 3 rule: it starts with `mod`. Every `*.yaml` file in `itemxl\` and its
subfolders is read. If you use a mod manager, the same layout goes into the mod it installs.

## 2. Write a change

Open `my_first.yaml` and make the crossbow bolts lighter and pricier:

```yaml
item:
  Bodkin Bolt:
    weight: 0.05
    price: 5
```

`item` is the kind of record, `Bodkin Bolt` is the record's name as the game files spell it, and the lines below it
are the fields to change. Everything you do not mention stays as it is. To find the name of a record and its fields,
use [`itemxl-cli search` and `itemxl-cli show`](10-cli.md):

```powershell
itemxl-cli search Bodkin
itemxl-cli show "Bodkin Bolt"
```

## 3. Start the game

ItemXL runs when the game starts. It notices that a YAML file changed, merges all changes into the game's
definitions, and writes the result into its own output mod, `Mods\mod00000_ItemXL`. The game then loads that output
like any other mod. If nothing changed since the last start, ItemXL keeps the output and the start is not slower.

## 4. Check the result

Open `Mods\mod00000_ItemXL\itemxl_report.txt`. Your file is listed near the top:

```
[delta]  modMyFirstItemXL: itemxl\my_first.yaml (1 records)
```

Further down, the report lists conflicts with other mods, errors, and warnings, such as a misspelt field name. In
the game, the TW3SE console (F2, Console tab) shows the same with `/itemxl report`, and `/itemxl show Bodkin Bolt`
prints the record as the game reads it now. See [Report and console](09-report-and-console.md).

If the last build had errors or warnings, a notice appears about two seconds after a save is loaded.

## 5. Make a new item

A new item is a copy of an existing one with your own changes. ItemXL creates it in its own DLC, together with the
New Game Plus copy, so you need no DLC folder of your own:

```yaml
item:
  My Steel Sword:
    $base: Long Steel Sword
    price: 250
    enhancement_slots: 2
    $text:
      name: {en: My Steel Sword, de: Mein Stahlschwert}
      description: {en: A plain sword with two rune slots.}
    $sell: [_store__Hattori_Master_Blacksmith]
```

`itemxl-cli new "My Steel Sword" --base "Long Steel Sword"` writes such a file for you. Details are in
[New items](05-new-items.md).

## Where to go next

- [Concepts](02-concepts.md) explains what ItemXL does with your file, which helps when something does not work.
- [YAML format](03-yaml-format.md) and [Operations](04-operations.md) cover everything a change can do.
- [Examples](14-examples.md) lists complete example mods in this repository.
