# YAML format

This page describes how an ItemXL file is laid out and how YAML maps to the game's XML. The operations that change
values (`!append`, `!scale`, …) have their own page: [Operations](04-operations.md).

## Where files go

| Place | Use |
|---|---|
| `<game>\Mods\<mod>\itemxl\**\*.yaml` | the changes of a mod; they follow the mod's place in the [load order](08-load-order-and-compatibility.md) |
| `<game>\bin\itemxl\**\*.yaml` | standalone changes that belong to no mod (like `r6\tweaks` for TweakXL); applied after all mods, sorted by file name |

`.yml` works as well. Subfolders are allowed and only serve your own order.

## Layout

```yaml
item:                                    # kind of record
  Bodkin Bolt:                           # record name
    price: 5                             # an attribute
    tags: !append [MyTag]                # a child element (a list)
ability:
  bodkin_bolt _Stats:
    PiercingDamage: {min: 90, max: 90}   # a stat
```

The first level is the kind of record, the second the record's name exactly as the game files spell it (case
counts), the third its fields. Kinds you will use most: `item`, `ability`, `loot` (loot and merchant lists),
`schematic` (crafting). Any other kind of record in the definition files works the same way.

## Attributes

XML attributes of the record are plain keys:

```yaml
item:
  Bodkin Bolt:
    price: 5          # sets price="5"
    weight: 0.05
    icon_path: null   # null removes the attribute
```

Numbers are written the way the game writes them (`0.05`, not `5e-2`).

## Child elements

Elements inside the record are keys as well. How a value is read depends on what kind of element it is:

| Element | Example XML | YAML |
|---|---|---|
| tag list | `<tags>A, B</tags>` | `tags: [A, B]` sets the whole list; operations change it |
| name list | `<base_abilities><a>X</a></base_abilities>` | `base_abilities: [X, Y]` |
| counted list | `<recycling_parts><parts count="2">Timber</parts>` | `recycling_parts: {Timber: 2, String: null}` sets or removes single entries |
| ingredient list | `<ingredients><ingredient quantity="2" item_name="Steel ingot"/>` | `ingredients: {Steel ingot: 2}` |
| anything else | any element | `player_override: {"!xml": "<player_override>...</player_override>"}` replaces it as XML |

Lists are changed with [operations](04-operations.md); a plain list replaces the whole list.

## Stats

An ability holds its values as stat elements, each with attributes:

```xml
<ability name="Long Steel Sword _Stats">
  <SlashingDamage type="base" min="120" max="140"/>
  <quality type="add" min="1" max="2"/>
</ability>
```

In YAML a stat is a mapping of the attributes you want to set. Attributes you leave out keep their value:

```yaml
ability:
  Long Steel Sword _Stats:
    SlashingDamage: {min: 150}             # min becomes 150, max stays 140
    PiercingDamage: {min: 90, max: 90}     # a stat the ability did not have is added
    quality: {type: add, min: 4, max: 4}   # type is part of the stat as well
    BleedingEffect: {is_ability: "true"}   # some stats carry other attributes; they work the same way
    SilverDamage: !scale 1.25              # multiplies min, max, and value; see Operations
```

`type` says how the game combines the stat with others: `base` is the starting value, `add` is added to it, `mult`
multiplies it. Copy the `type` the game uses for that stat (see it with [`itemxl-cli show`](10-cli.md#show)).

Write a stat always as a mapping (or with `!scale` / `!offset`). A plain number such as `SlashingDamage: 150` would be
read as an attribute of the ability itself, not as the stat.

## Only one copy of a record

A record usually has several [copies](02-concepts.md#copies-of-a-record), and ItemXL changes the ones the game uses.
To change exactly one file, add it to the name after ` | `:

```yaml
item:
  Bodkin Bolt | gameplay\items_plus\def_item_bolts.xml:
    price: 7          # New Game Plus only
```

## Removing and replacing a record

```yaml
item:
  Some Item: null                                                 # removes the record
  Other Item:
    $xml: '<item name="Other Item" category="junk" price="1"/>'   # replaces the record completely
```

Use both rarely: other mods may rely on the record.

## Several documents in one file

A file may hold several YAML documents, separated by a line with `---`. Each document is read on its own. This is
useful together with [`$if`](06-rules.md#if), which then applies to one document only:

```yaml
item:
  Bodkin Bolt:
    price: 5
---
$if: {mod: modOtherBolts}           # only this part depends on another mod
item:
  Bodkin Bolt:
    weight: 0.05
```

## Anchors

YAML anchors write a value once and reuse it: `&name` marks it, `*name` repeats it.

```yaml
item:
  Sword A:
    $sell: &smiths [_store__Hattori_Master_Blacksmith, _store__Rich_District_Armorer]
  Sword B:
    $sell: *smiths
```

## Keys with `$`

Keys starting with `$` are instructions to ItemXL, not fields of the record. Each has its own page:

| Keys | Explained in |
|---|---|
| `$base`, `$xml`, `$file`, `$instances`, `$text`, `$sell`, `$loot`, `$recipe`, `$alchemy` | [New items](05-new-items.md) |
| `$where`, `$if`, `$requires`, `$after` | [Rules](06-rules.md) |
| `$appearance` | [Appearance](07-appearance.md) |

## Checks

ItemXL checks every record a file names against the game: unknown attributes and stats, abilities that do not exist,
unknown merchants, ingredients, and craftsmen. These are warnings; the change is still made.

A field name that cannot be an XML name, for example one with a space in it, is an error: that field is left out and
the rest of the record is changed. Written into a definition file, such a name would make the whole file unreadable
for the game.

A YAML syntax error is different: the whole file is skipped, and the report shows the file and line under `ERRORS`.
The game then runs as if the file were not there, so a broken file cannot break the game. All other files and mods
are built as usual. See [Report and console](09-report-and-console.md).

Next: [Operations](04-operations.md).
