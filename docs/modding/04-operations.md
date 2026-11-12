# Operations

A plain value replaces what the game has. Operations change it instead: add to a list, take something out, multiply
a number. They are what lets several mods change the same record without overwriting each other.

## Two ways to write an operation

**As a YAML tag**, the way TweakXL writes it. The tag stands in front of the value:

```yaml
tags: !append [MyTag]                  # adds MyTag
```

A field can carry only one tag this way.

**As a mapping** with the operation as key. This allows several operations on the same field:

```yaml
tags: {"!append": [MyTag], "!remove": [InfiniteAmmo]}    # adds MyTag and removes InfiniteAmmo
```

The quotes are needed because a key may not start with `!` in YAML otherwise. `!append` written either way does the
same. In a list, each entry can carry its own tag, which is another way to do several things at once:

```yaml
base_abilities:
  - !remove Old ability
  - !append New ability
```

## Lists

Two kinds of elements hold lists: `<tags>`, a comma-separated list of tags, and elements such as `<base_abilities>`,
which hold one name per entry (`<a>Name</a>`). Both are changed with these operations:

| Operation | Effect |
|---|---|
| `!append [A, B]` | adds A and B at the end of the list |
| `!prepend [A, B]` | adds A and B at the start of the list |
| `!append-once`, `!prepend-once` | the same, but only adds what is not in the list yet |
| `!remove [A]` | takes every A out of the list |
| `!set [A, B]` | replaces the whole list (the same as writing a plain list) |
| `!append-from [Other Record]` | takes the entries of the same list of another record and adds them at the end |
| `!prepend-from [Other Record]` | takes the entries of the same list of another record and adds them at the start |

Tags are never added twice, so `!append` on tags works like `!append-once`. In `base_abilities` an entry may appear
twice, so `!append` there really appends.

```yaml
item:
  Bodkin Bolt:
    tags: {"!append": [mod_legendary], "!remove": [InfiniteAmmo]}
  My Bolt:
    $base: Blunt Bolt
    tags: !append-from [Bodkin Bolt]      # gets every tag Bodkin Bolt has, at the end of its own
```

`!append-from` reads the other record as it is at that moment of the build, with the changes of mods applied before.
The other record must be of the same kind (an item from an item, an ability from an ability).

## Numbers

| Operation | Effect |
|---|---|
| `!scale 1.25` | multiplies |
| `!offset 10` | adds (a negative number subtracts) |

```yaml
item:
  Long Steel Sword:
    price: !scale 1.5
ability:
  Long Steel Sword _Stats:
    SlashingDamage: !scale 1.1     # min, max, and value of the stat
    weight: !offset -0.5
```

`!scale` and `!offset` from several mods add up: two mods with `!scale 1.1` give `1.21`. They are never a
conflict. Setting a plain value is: if two mods set the same field to different values, the report lists a conflict
and the load order decides (see [Load order and compatibility](08-load-order-and-compatibility.md)).

## Removing

```yaml
item:
  Bodkin Bolt:
    icon_path: null   # removes the attribute
    tags: null        # removes the whole element
```

## Merchant and loot entries

Loot lists hold entries with names. `!append` adds new entries and leaves the others in place:

```yaml
loot:
  _store__Hattori_Master_Blacksmith:
    loot_entry: !append
      - {name: Silver Bolt, quantity_min: 1, quantity_max: 3}
```

Always add with `!append`. Other forms change existing entries instead of adding, and could take away what the game
and other mods put into the list. For selling or dropping a new item there are shorter ways, `$sell` and `$loot`;
see [New items](05-new-items.md#merchants-loot-and-recipes).

## XML as it is

For elements ItemXL has no YAML form for, `!xml` replaces the element with the XML you give:

```yaml
item:
  My Sword:
    player_override:
      "!xml": "<player_override><bound_items><item>scabbard_steel_3_06</item></bound_items></player_override>"
```

`!xml` sets the whole element. Two mods that both use it on the same element are a conflict.

## What adds up and what conflicts

| Change | Two mods do it on the same field |
|---|---|
| plain value, `!set`, `!xml`, `null` | conflict if the values differ; the load order decides |
| `!append`, `!prepend`, `!remove` | both apply |
| `!scale`, `!offset` | both apply, in load order |

Next: [New items](05-new-items.md).
