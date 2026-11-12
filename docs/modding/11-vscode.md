# VS Code

ItemXL makes a JSON schema from the game's own files. With it, VS Code autocompletes record names, attributes, stats,
merchants, and ItemXL's own keys while you type, and underlines mistakes before you start the game.

## Setup

1. Install the **YAML** extension by Red Hat.
2. Run once for your mod folder (the folder you open in VS Code):

   ```powershell
   itemxl-cli schema --vscode "C:\path\to\my mod"
   ```

   This writes `.vscode\itemxl.schema.json` and `.vscode\settings.json`. The settings link the schema to every
   `itemxl\*.yaml` and register ItemXL's tags (`!append`, `!scale`, …) so the extension accepts them. If a
   `settings.json` already exists, it is left alone and the lines to add are printed.
3. Open the folder in VS Code (File → Open Folder).

Run the command again after a game update or a new ItemXL version; the schema lists what the game has.

## What it does

- **Record names can be autocompleted.** After `$base:`, in ingredient lists, and in merchant lists, VS Code suggests the
  names the game has.
- **Fields can be autocompleted.** Inside a record it suggests the attributes, child elements, and stats that exist for that
  kind of record. Where the game only knows a fixed set of values, it offers them: craftsmen, crafting levels,
  diagram rarities.
- **Mistakes are underlined in red:** unknown keys, misspelt record names, and YAML syntax errors.

The schema knows the game's records, not those other mods add. A new record of another mod is underlined but still
works; the [report](09-report-and-console.md) is the final check.

Next: [Scripts](12-scripts.md).
