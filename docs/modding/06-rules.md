# Rules

A normal change names one record: `Bodkin Bolt` gets a new price. A rule works differently. It either describes
which records to change (`$where`), or says when a change should apply at all (`$if`, `$requires`, `$after`). With
rules a mod can work together with other mods, even ones its author does not know.

## `$where`

A filter instead of a name. The change goes to every record of that kind that matches:

```yaml
item:
  Every crossbow:                       # only a label: shown in the report
    $where: {category: crossbow}
    price: !scale 1.2
```

The key (`Every crossbow`) is only a label. Use a name that says what the rule does; it should not be the name of a
real record.

| Filter | Matches when the record … |
|---|---|
| `name: "Crossbow*"` | has a name that fits the pattern (`*` any text, `?` one character); a list means any of them |
| `category: steelsword` | has that category; a list means any of them |
| `tags: [A, B]` | has all of these tags |
| `tags_any: [A, B]` | has at least one of them |
| `tags_none: [A, B]` | has none of them |
| `ability: [X]` | has all of these base abilities |
| `has: [SlashingDamage]` | has these child elements, for example stats of an ability |
| `price: ">500"` | has an attribute with that value; `>`, `>=`, `<`, `<=`, `=`, `!=` compare numbers |

All filters of one `$where` must match. Letters are compared without case.

An empty filter, `$where: {}`, matches every record of the kind. That is rarely what you want: it also changes quest
items, technical items, and the new items of every other mod. Narrow it down with at least a category or a tag.

Because ItemXL creates all new records first and applies changes after, `$where` also finds the new items of other
mods. A rule like "every item with the tag `Aerondight` gets 5 rune slots" also reaches a new Aerondight variant that
another mod adds.

The report lists what each rule matched:

```
[where]  modMyBalance: item 'Every crossbow': 12 record(s): Bear School Crossbow, Crossbow 1, Crossbow 2, ...
```

A rule that matches nothing is a warning, so a typo in a filter does not go unnoticed.

## `$if`

A condition. At the top of a document it applies to the whole document, inside a record to that record only:

```yaml
$if: {mod: modBetterCrossbows}             # the whole document
item:
  My Crossbow:
    $if: {not: {mod: modOtherCrossbows}}   # this record only
    price: 500
```

| Condition | Holds when |
|---|---|
| `{mod: modX}` | modX is installed and enabled |
| `{mod: [modX, modY]}` | all of them are installed and enabled |
| `{any: [{mod: modX}, {mod: modY}]}` | at least one of them is installed (an "or") |
| `{not: {mod: modX}}` | modX is not installed |
| `{item: Name}`, `{ability: Name}`, … | the record exists, in the game or as a new record of any mod |
| a list of conditions, or several keys | all of them hold |

Skipped parts are listed in the report as `[if]`. With [several documents in one file](03-yaml-format.md#several-documents-in-one-file)
(separated by `---`), each document can have its own `$if`:

```yaml
item:
  My Crossbow:
    $base: Crossbow 1
    price: 400
---
$if: {mod: modBetterCrossbows}         # only this document depends on the other mod
item:
  My Crossbow:
    price: 600
```

## `$requires`

```yaml
$requires: [modBaseItems]
```

The file needs these mods. If one is missing or disabled, the whole file is skipped and the report shows an error
that names the missing mod, instead of many follow-up errors about records that do not exist.

## `$after`

```yaml
$after: [modBalanceA]
```

This file is applied after modBalanceA's files, so where both set the same value, this file wins, whatever the
player's load order says. Use it for patches made for another mod. The report shows `[after]`; `$after` lists that
contradict each other are reported and change nothing further.

## Example: a patch inside your own mod

A mod adds a crossbow. Another mod, `modBetterCrossbows`, raises the price of every crossbow by half. If both are
installed, the new crossbow should follow:

```yaml
item:
  My Hunting Crossbow:
    $base: Crossbow 2
    price: 300
    $text:
      name: {en: Hunting Crossbow}

  Hunting Crossbow with Better Crossbows:
    $if: {mod: modBetterCrossbows}
    $where: {name: My Hunting Crossbow}
    price: !scale 1.5
```

Without `modBetterCrossbows` the second entry is skipped and listed as `[if]`. Players need no separate patch mod.

Next: [Appearance](07-appearance.md).
