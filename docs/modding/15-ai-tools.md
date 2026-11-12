# Writing mods with AI tools

AI assistants and coding agents can write ItemXL YAML. They know YAML well, but they do not know the names the
game uses for its records, stats, and merchants. Most mistakes come from there: a name that sounds right but does not
exist. Give the tool the facts, and check its result with ItemXL.

## What to give the tool

1. **This documentation.** Especially [YAML format](03-yaml-format.md), [Operations](04-operations.md),
   [New items](05-new-items.md), and [Rules](06-rules.md); for looks also [Appearance](07-appearance.md).
2. **The records you work with**, as the game has them:

   ```powershell
   itemxl-cli show "Long Steel Sword"
   itemxl-cli show "Long Steel Sword _Stats" --tag ability
   itemxl-cli search _store_ --tag loot
   ```

   Paste the output into the chat. With it the tool sees the real field names, stats, and values.
3. **For larger work, the schema**: `itemxl-cli schema` writes `itemxl.schema.json`, the list of every record name,
   attribute, and stat the game has. A tool that can read files can look names up there.

Tell the tool to use only names it found in what you gave it, and to ask instead of guessing.

## Checking the result

Never put generated YAML into a mod without checking it:

```powershell
itemxl-cli build --mods "C:\path\to\test\Mods" --out "C:\path\to\test-output" --force
```

Then read `itemxl_report.txt` in the output folder ([Report and console](09-report-and-console.md)). Unknown
attributes, stats, abilities, merchants, and ingredients are listed there. You can paste the report back into the
chat and let the tool fix its file. [VS Code](11-vscode.md) with the schema underlines the same mistakes while you
look at the file.

## Things AI tools often get wrong

- **Stats as plain numbers.** `SlashingDamage: 150` does not set the stat; it has to be `{min: 150}` or
  `!scale`/`!offset` ([Stats](03-yaml-format.md#stats)).
- **TweakXL habits.** `$type`, dotted record IDs, or `Items.` prefixes do not exist in ItemXL
  ([From TweakXL](../tweakxl-archivexl/02-from-tweakxl.md)).
- **Invented merchants and loot lists.** Only names from `itemxl-cli search _store_ --tag loot` work.
- **Replacing lists by accident.** A plain list replaces the whole list; adding needs `!append`
  ([Operations](04-operations.md#lists)).
- **Script names with brackets** when the tool also writes WitcherScript ([Scripts](12-scripts.md#item-names-in-scripts)).

The game itself is the last check: test the mod in a save before you publish it.

Next: back to the [documentation index](../README.md).
