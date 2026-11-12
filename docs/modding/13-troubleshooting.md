# Troubleshooting

Start with the [report](09-report-and-console.md): most problems are named there. For script problems, the TW3SE log
(`<game>\bin\x64_dx12\tw3se\tw3se.log`) has the compiler's messages.

## The game ends right after loading scripts

A script does not compile. The TW3SE log names the file and line. Common causes:

- An event wrapped with a return type. Events always return `bool` implicitly; write
  `@wrapMethod(CR4Player) function OnHolsteredItem(category : name, slotName : name)` without `: bool`.
- A name literal with brackets or other signs, such as `'My Sword (Steel)'`; see [Scripts](12-scripts.md#item-names-in-scripts).
- Two mods define the same field or method.

## My YAML file is not in the report

- Is it in `Mods\<mod>\itemxl\` and does it end in `.yaml` or `.yml`?
- Is the mod enabled in `mods.settings`?
- A syntax error skips the whole file: look under `ERRORS` for the file name and line. A typical one is a colon too
  many, or a key indented under the wrong record.
- A `$requires` that does not hold skips the file with an error; an `$if` that does not hold shows as `[if]`.

## A change does not show in the game

- Check the record with `/itemxl show <name>` in the TW3SE console: it shows what ItemXL wrote.
- Another mod may set the same field later: look for the record under `CONFLICTS`.
- A mod listed in `mods.settings` that replaces the whole definition file may win over ItemXL's result. The report
  warns about it; see [The output mod and `mods.settings`](08-load-order-and-compatibility.md#the-output-mod-and-modssettings).
- A DLC copy with `on_conflict="extend"` or `"replace"` may win in the game; ItemXL handles them, see
  [XML override](08-load-order-and-compatibility.md#xml-override). Look under "stacked / overridden changes".
- A script may change the value at runtime; ItemXL cannot see that.
- `$base` copies the game's record, not what other mods made of it. A balance mod's change to the original does not
  reach your copy; set the value yourself or use [`$if`](06-rules.md#example-a-patch-inside-your-own-mod).

## A new item is missing or nameless

- Not at the merchant: is the loot list name right (`itemxl-cli search _store_ --tag loot`)?
- No name: `$text` needs at least `en`. Warnings like "new item … has no localisation_key_name" point to it.
- `$where` rules matching nothing are warnings; check the filter.

## An item looks wrong

- Invisible model: the `.buffer` file next to the `.w2mesh` is missing from `itemxl\files\`.
- Old look with gloves on: set `gloves` in [`$appearance`](07-appearance.md).
- No scabbard on a sword moved between slots: the bound scabbard has the category of the old slot
  (`silver_scabbards` / `steel_scabbards`). Bind one of the right kind through `player_override`.
- Icon blank or missing: a DXT5 `.dds` of 64×128 (weapons, armor) or 64×64, set with `icon`.

## Nothing seems to happen at game start

- Is `ItemXL.dll` in `<game>\bin\x64_dx12\tw3se\plugins\` and TW3SE installed? The TW3SE log lists loaded plugins.
- If nothing changed, ItemXL keeps the last output on purpose. Delete `Mods\mod00000_ItemXL\itemxl.stamp` to force a
  build.

Next: [Examples](14-examples.md).
