# From TweakXL

ItemXL follows TweakXL's model: YAML files that change records, merged by a framework instead of shipped as replaced
files. This page lists what corresponds to what, and where the two differ.

## Data

| Cyberpunk 2077 / TweakXL | The Witcher 3 / ItemXL |
|---|---|
| TweakDB, a database in memory | definition XML files (`gameplay\items\*.xml`, `gameplay\abilities\*.xml`, and the same in DLCs) |
| record, for example `Items.Preset_Lexington_Default` | record, for example `Long Steel Sword` |
| record type such as `gamedataItem_Record`, set with `$type` | the XML element of the record (`item`, `ability`, `loot`, `schematic`, …), written as the first level of the YAML |
| flat, for example `Items.X.quality` | attribute (`price`), child element (`tags`, `base_abilities`), or stat of an ability (`SlashingDamage`) |
| TweakDBID, a hashed path | the name as the files spell it, with spaces |
| `r6\tweaks\*.yaml` | `Mods\<mod>\itemxl\*.yaml`, or `bin\itemxl\*.yaml` for standalone files |

In Witcher 3 a record usually exists several times: in `items` for the normal game and in `items_plus` for New Game
Plus, and again in DLCs. A YAML change names the record once; which copies it is applied to is described in
[Concepts](../modding/02-concepts.md#copies-of-a-record) and
[XML override](../modding/08-load-order-and-compatibility.md#xml-override).

## Syntax

```yaml
# TweakXL
Items.MyKnife:
  $base: Items.Preset_Knife_Default
  quality: Quality.Legendary
  tags: !append [MyTag]

# ItemXL
item:
  My Knife:
    $base: Long Steel Sword
    quality: 5
    tags: !append [MyTag]
```

| TweakXL | ItemXL |
|---|---|
| `$type` | the first level of the YAML (`item:`, `ability:`) |
| `$base` | `$base`; copies the record as the game files have it, without other mods' changes |
| `$instances`, `$(key)` | the same |
| `!append`, `!append-once`, `!prepend`, `!prepend-once`, `!remove` | the same |
| `!append-from`, `!prepend-from` | the same |
| not available | `!scale`, `!offset`: multiply or add; changes of several mods add up ([Operations](../modding/04-operations.md)) |
| not available | `$where`, `$if`, `$requires`, `$after` ([Rules](../modding/06-rules.md)) |
| localization through ArchiveXL | `$text` on the record ([New items](../modding/05-new-items.md#text-name-and-description)) |
| not available | `$sell`, `$loot`, `$recipe`, `$alchemy`: merchant, loot, crafting, and alchemy records |

## Behavior

| | TweakXL | ItemXL |
|---|---|---|
| when changes are applied | while the game runs, to TweakDB in memory | at game start, before the game reads its files; the result is a mod the game loads |
| reloading | `TweakXL.Reload()` | restart the game |
| mods that replace data files | not applicable | imported and turned into changes ([more](../modding/08-load-order-and-compatibility.md#mods-that-replace-whole-files)) |
| two mods set the same value | the later one wins | the winner follows the game's mod order; the report lists the conflict |
| load order | the files of `r6\tweaks` as the folder lists them (alphabetical on Windows); names starting with `_`, `#`, `$`, `!` first, `^` last | the game's mod order ([Load order](../modding/08-load-order-and-compatibility.md#the-load-order)) |
| errors and unknown names | log | report, notice in the game, VS Code schema ([Report](../modding/09-report-and-console.md)) |

## Looking at records

| Cyberpunk 2077 | The Witcher 3 |
|---|---|
| CET: `TweakDB:GetRecord()`, `TweakDB:GetFlat()` | TW3SE console: `/itemxl show <name>` |
| WolvenKit's TweakDB browser | `itemxl-cli search`, `itemxl-cli show` ([Command-line tool](../modding/10-cli.md)) |

Next: [From ArchiveXL](03-from-archivexl.md).
