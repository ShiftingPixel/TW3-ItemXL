# Game behavior

ItemXL relies on how the game loads data. Where that is not documented, it was tested in the game. This page lists
what was confirmed and what the code does because of it. When you change one of these areas, test it in the game
again; passing tests are not enough.

## Loading

| Behavior | Consequence in the code |
|---|---|
| TW3SE runs plugins before the game compiles scripts and reads mods; the TW3SE author has confirmed this stays so | the build at plugin load is in time for the same game start |
| A new definition XML in a mod's `gameplay\items\` is not read; new files need a DLC (`.reddlc` with a mounter) | new records go into ItemXL's DLC `dlcitemxl` |
| The game finds a `.reddlc` in any bundle, also in `Mods\` | ItemXL's DLC lives in its output mod; DLCs inside mods' bundles are read as copies |
| The mod with the lowest `Priority` in `mods.settings` wins; unlisted mods follow by name; without `mods.settings` the mods are sorted by name | `mod_order` reproduces this order and applies the winner last; the build warns when a listed mod would win over the output mod with a file ItemXL merged |

## Copies of a record

| Behavior | Consequence |
|---|---|
| `items` and `items_plus` (and `abilities_plus`) are separate sets for the normal game and New Game Plus | a change goes to the first copy of each set |
| A DLC copy with `on_conflict="replace"` replaces the record | changes go to it as well (`targets`) |
| A DLC copy with `on_conflict="extend"` lays its attributes and elements over the record; **tags are joined, `base_abilities` replace the record's** (tested with a DLC copy of `Bodkin Bolt`) | `apply_extends`: fields the copy sets get the change; for tags only `!remove` and new lists, for `base_abilities` all of it; other lists are reported |
| A copy without `on_conflict` only adds (merchant goods, extra loot) | such fragments are left alone, otherwise entries would be doubled |

## Appearance

| Behavior | Consequence |
|---|---|
| Item templates in a DLC are only found if the `.reddlc` mounts their folder | `CR4DefinitionsEntitieTemplatesDLCMounter` in ItemXL's DLC |
| Model geometry in a `.buffer` is only read from `buffers0.bundle` | `.buffer` files are packed there; otherwise the model is invisible |
| With gloves worn the game uses the item's `<variant>` template | the variant gets its own copy |
| Inventory icons are looked up in the texture cache by the hash of their path, not by an `.xbm` | icon entries use `texture_key(path)`; `icon_path` points to `itemxl/<name>.png` |
| Armors of quality 5 can be dyed if their template has `dye_*` appearances | copies keep them; missing ones are warned about |

## Scripts

| Behavior | Consequence |
|---|---|
| A wrapped `event` must not declare a return type | generated and example scripts follow it |
| Name literals allow letters, digits, and spaces only, and there is no string-to-name conversion | documented for authors; scripts find such items by tag |

## Loot

How a loot list hands out items, read from the game's code (`CR4LootDefinitions`):

- The number of entries is random, between the list's own `quantity_min` and `quantity_max`.
- Entries with `chance="-1"` come first. The others are drawn without replacement, weighted by `chance`.
- If the list has no more entries than that number, all of them are given.
- Entries outside the player's level range are left out before.

## Measured

| What | Value |
|---|---|
| Start without changes (250 mods, mod manager with virtual file system) | under 1 s, mostly listing mod folders |
| Start with a rebuild | a few seconds, about 1 s of it reading and parsing the game's definition files |

Next: [Guidelines](06-guidelines.md).
