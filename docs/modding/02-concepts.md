# Concepts

This page explains what ItemXL does with your YAML files. Knowing it makes the report and most problems easy to
understand.

## Definition files and records

The game describes items, abilities, crafting diagrams, merchants, and loot in XML **definition files**, for example
`gameplay\items\def_item_bolts.xml`. Each entry in such a file is a **record**:

```xml
<item name="Bodkin Bolt" category="bolt" price="0" weight="0.1" ...>
  <tags>WeaponTab, InfiniteAmmo, GroundAmmo, bolt</tags>
  <base_abilities>
    <a>bodkin_bolt _Stats</a>
  </base_abilities>
</item>
```

A record has a **kind** (the XML tag: `item`, `ability`, `loot`, `schematic`, …) and a **name**. In YAML you address
a record by both:

```yaml
item:            # kind
  Bodkin Bolt:   # name
    price: 5     # field
```

## Changes instead of files

Without ItemXL, a mod that changes one value has to ship the whole definition file. Two such mods replace each other's
file, and only one of them works. With ItemXL, each mod only states its changes. ItemXL applies all of them to the
game's records, one field at a time. Two mods only collide when they set the **same field** of the **same record** to
different values. That is a **conflict**; the report names it and the load order decides who wins (see
[Load order and compatibility](08-load-order-and-compatibility.md)).

Mods that still ship whole definition files are not lost: ItemXL compares their files with the game's and turns the
difference into changes, as if the mod had been written in YAML.

## Copies of a record

Many records exist more than once:

- **Base game and New Game Plus.** Most files exist twice, in `items\` and `items_plus\` (and `abilities\` /
  `abilities_plus\`). ItemXL changes both copies, so your change also works in New Game Plus.
- **DLCs.** Hearts of Stone, Blood and Wine, the free DLCs, and DLCs of mods can define the same record again. A copy
  marked `on_conflict="replace"` replaces the record, one marked `on_conflict="extend"` lays its fields over it, and a
  copy without that mark only adds to it (for example more goods for a merchant). ItemXL changes the copies that decide
  what the game uses. [Load order and compatibility](08-load-order-and-compatibility.md#xml-override) has the details.

You do not need to know which file a record lives in. ItemXL finds every copy by kind and name.

## The build at game start

ItemXL is a plugin of [TW3SE](https://www.nexusmods.com/witcher3/mods/13837). TW3SE loads it when the game starts,
before the game reads its mods. ItemXL then:

1. Checks whether anything changed since the last start: the game files, the mods, their YAML files, and the load
   order. If nothing changed, it stops here.
2. Reads the game's definition files and those of the DLCs.
3. Collects the changes: YAML files of the mods, whole-file mods turned into changes, and standalone YAML files in
   `<game>\bin\itemxl\`.
4. Creates all **new records** of all mods.
5. Applies all **changes**, mod by mod in load order.
6. Builds custom appearances, checks the result, and writes the report.
7. Writes everything into the output mod.

Because new records are created before any change is applied, a mod can change an item that another mod adds, no
matter which of the two loads first.

## The output mod

ItemXL writes its result into `<game>\Mods\mod00000_ItemXL`:

| File | Content |
|---|---|
| `content\blob0.bundle` | the changed definition files and ItemXL's own DLC with the new records |
| `content\buffers0.bundle`, `content\texture.cache` | models and textures of [custom appearances](07-appearance.md) |
| `content\scripts\local\itemxl.ws` | a small script for the notices after loading a save |
| `tw3se\strings\*.txt` | names and descriptions from `$text`, one file per language |
| `itemxl_report.txt` | the [report](09-report-and-console.md) |
| `itemxl.stamp` | what the last build was made from; used to skip unchanged starts |
| `itemxl.items` | the new items of the last build; used to report items that are gone at the next build |

Never edit these files: the next build replaces them. Deleting the folder is safe; ItemXL builds it again at the next
start. The installed game files are never changed.

## What ItemXL does not do

- It does not change records while the game runs. A change takes effect at the next game start.
- It does not make models or textures. It packs the ones you bring (see [Appearance](07-appearance.md)).
- It does not merge scripts. Script mods are a separate topic (see
  [Load order and compatibility](08-load-order-and-compatibility.md#script-mods)).

Next: [YAML format](03-yaml-format.md).
