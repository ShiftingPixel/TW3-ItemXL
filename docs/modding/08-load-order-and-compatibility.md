# Load order and compatibility

## The load order

When two mods change the same value, one of them has to win. ItemXL decides this the same way the game decides
between two mods that bring the same file, so the result matches what players expect from their mod order. ItemXL
reads the order; it never writes or changes it.

- If `Documents\The Witcher 3\mods.settings` exists, it lists mods with `Enabled` and `Priority`. The mod with the
  **lowest `Priority`** wins. Mods with `Enabled=0` are left out. Mods missing from the file come after the listed
  ones, sorted by name.
- If there is no `mods.settings`, the game sorts the mods by folder name, and so does ItemXL.
- Standalone files in `<game>\bin\itemxl\` come after all mods.
- Within a mod, its files are applied in the order of their paths.

ItemXL applies the changes in this order from the losing end to the winning end, so the change of the winning mod is
the one that stays.

### The output mod and `mods.settings`

The game has to read ItemXL's result instead of the files it was merged from, so the output mod comes first. By name
the game already sorts it there, before the two other mods that tools create:

| Priority | Mod | Made by |
|---|---|---|
| 0 | `mod00000_ItemXL` | ItemXL |
| 1 | `mod00000_TW3SE_Files` | TW3SE, only when you use its loose files (`tw3se\files\`) |
| 2 | `mod0000_MergedFiles` | Script Merger |
| 3+ | every other mod | |

- **Script Merger:** use it as usual and merge everything it finds. ItemXL does not read `mod0000_MergedFiles`: it
  reads the definition files of the original mods and merges them itself, field by field. The definition files
  Script Merger merged are then left unused, as long as ItemXL comes first.
- **TW3SE's loose files:** ItemXL reads `mod00000_TW3SE_Files` like any other mod, so a definition file put there is
  merged too.

A mod that **is** listed in `mods.settings` comes before the ones that are not. If such a mod, or
`mod0000_MergedFiles`, replaces a definition file ItemXL writes and comes before the output mod, the game reads that
file and not ItemXL's result. ItemXL does not change `mods.settings`; the report warns when this happens and names
the mod. The fix is an entry for the output mod with a `Priority` below every other mod's:

```
[mod00000_ItemXL]
Enabled=1
Priority=0
```

## Conflicts

A conflict is two mods setting the same field of the same record to different values. The report lists each one and
names the winner:

```
CONFLICTS: 1
  item/Bodkin Bolt/price: modA = '5'  vs  modB = '7'  -> modB wins
```

Operations that add up (`!append`, `!remove`, `!scale`, `!offset`) are never conflicts; see
[Operations](04-operations.md#what-adds-up-and-what-conflicts). To make a patch win regardless of the player's
order, use [`$after`](06-rules.md#after).

## Mods that replace whole files

Many mods ship complete definition files in their `content\*.bundle`. ItemXL compares each such file with the game's
and turns the difference into changes, which then go through the same load order and conflict check as YAML. The
report lists them as `[import]`.

If such a file lacks records the game has (often a mod made for an older game version), ItemXL keeps the game's
records and warns, instead of letting the old file delete them.

## XML override

The Witcher 3 Remastered (patch 5.0) added a way for DLCs to redefine single records. A record in a DLC's definition
file can carry an `on_conflict` attribute:

| `on_conflict` | What the game does when the record exists already |
|---|---|
| not set | according to CD PROJEKT RED, a redefinition is an error. The game's own DLCs use such copies to add entries, for example more goods for a merchant. |
| `replace` | the new definition replaces the old one completely |
| `extend` | the elements of the new definition replace the matching ones of the old definition; the rest stays. In the game, tags of both are joined, while `base_abilities` of the new definition replace the old ones. |

The game merges these copies itself; ItemXL does not manage them. What ItemXL does is make sure that **ItemXL's
own changes** still arrive in the game when such copies exist:

- **`replace` copies:** ItemXL makes its change in the game's record and in every `replace` copy, so the change is
  there whichever copy the game uses.
- **`extend` copies:** if an `extend` copy sets the same field as your change, ItemXL makes the change in that copy
  too. Otherwise the copy would put its own value back. Fields the copy does not set stay out of it.
- **Lists in `extend` copies:** for tags, only removals (and a completely new list) go to the copy, since the game
  joins the tags anyway. For `base_abilities`, the whole change goes to the copy, since the copy's list is the one
  that counts. Other lists in `extend` copies are left as they are, and the report warns that the change may not
  arrive.

The report lists every such case under "stacked / overridden changes".

DLCs count wherever they are: in `<game>\dlc\`, or inside a mod's bundle in `Mods\` (the game finds a `.reddlc` in
any bundle).

## Script mods

ItemXL changes data, not scripts. Its own script `itemxl.ws` only uses annotations, so it needs no script merging and
works next to mods that replace vanilla scripts. Values that scripts change while the game runs are outside ItemXL's
merge and report. See [Scripts](12-scripts.md).

## Mods that build on other mods

- A YAML file can change the new records of another mod like the game's own; the order of the two mods does not
  matter for that, because new records are created first.
- Name the other mod in [`$requires`](06-rules.md#requires) when your file makes no sense without it, and use
  [`$if`](06-rules.md#if) for parts that only apply when it is installed.
- Prefer tags and [`$where`](06-rules.md#where) over long lists of the other mod's record names: a renamed record
  then does not break your mod.
- If the other mod changes a value you set, decide who wins with [`$after`](06-rules.md#after) instead of telling
  players to sort their mods.

Next: [Report and console](09-report-and-console.md).
