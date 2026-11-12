# Witcher 3 for Cyberpunk modders

The Witcher 3 runs on REDengine 3, the predecessor of Cyberpunk 2077's REDengine 4. Many ideas are the same; files,
folders, and tools differ. This page covers what you need to make sense of ItemXL. The two pages after it compare
ItemXL with [TweakXL](02-from-tweakxl.md) and [ArchiveXL](03-from-archivexl.md).

## Folders

| Cyberpunk 2077 | The Witcher 3 |
|---|---|
| `archive\pc\mod\*.archive` | `Mods\mod<Name>\content\*.bundle`, with `metadata.store` and, for textures, `texture.cache` |
| `mods\<name>\` (REDmod) | `dlc\<name>\` for DLC-style mods; a mod's bundle in `Mods\` can hold a DLC as well |
| `r6\scripts\` | `Mods\mod<Name>\content\scripts\` |
| `red4ext\plugins\` | `bin\x64_dx12\tw3se\plugins\` (TW3SE plugins; ItemXL is one) |
| `r6\tweaks\` | `bin\itemxl\` for standalone ItemXL files |

## Load order

When two mods bring the same file, the game decides by their order:

- If `Documents\The Witcher 3\mods.settings` exists, it lists mods with `Enabled` and `Priority`; the lowest
  `Priority` wins. Mods not listed there come after the listed ones, sorted by name.
- Without `mods.settings`, the mods are sorted by folder name.

ItemXL uses the same order for its merge ([Load order and compatibility](../modding/08-load-order-and-compatibility.md)).

## Data

Items, abilities, crafting, merchants, and loot are records in XML **definition files**, not in a database. A DLC adds
its own files through a `.reddlc` definition that mounts them.

Since the Remastered patch (5.0), a record in a DLC can carry `on_conflict`, which the game uses when the record
exists already:

| `on_conflict` | Effect |
|---|---|
| not set | a redefinition is an error |
| `replace` | the new definition replaces the old one |
| `extend` | the elements of the new definition replace the matching ones of the old one |

This is the game's own way to change single records, comparable to REDmod tweaks. How ItemXL works with it:
[XML override](../modding/08-load-order-and-compatibility.md#xml-override).

## Scripts

| Cyberpunk 2077 | The Witcher 3 |
|---|---|
| Redscript | WitcherScript, compiled by the game at start from the loose `.ws` files of all mods |
| `@wrapMethod`, `@addMethod`, `@addField`, `@replaceMethod` | the same annotations; since 5.0 also for events and states, and native functions can be wrapped |
| CET (Lua) | TW3SE's Lua console and Lua mods |

In Witcher 3 many mods still replace whole vanilla script files. Two such mods collide, and players merge the files
with a script merging tool. Annotations avoid that; see [Scripts](../modding/12-scripts.md).

## Tools

| Cyberpunk 2077 | The Witcher 3 |
|---|---|
| [RED4ext](https://github.com/wopss/RED4ext) | [TW3SE](https://www.nexusmods.com/witcher3/mods/13837) |
| WolvenKit | [WolvenKit 7](https://github.com/WolvenKit/WolvenKit-7-nightly) and the official [REDkit](https://cdprojektred.atlassian.net/wiki/spaces/W3REDkit) |
| TweakXL, ArchiveXL | ItemXL ([From TweakXL](02-from-tweakxl.md), [From ArchiveXL](03-from-archivexl.md)) |

## Texts

The game's texts are `.w3strings` files, one per language. ItemXL writes its texts through TW3SE instead, so a mod
needs no `.w3strings` of its own ([`$text`](../modding/05-new-items.md#text-name-and-description)).

Next: [From TweakXL](02-from-tweakxl.md).
