# Architecture

ItemXL is one C++17 core compiled into two programs: the TW3SE plugin `ItemXL.dll` and the command-line tool
`itemxl-cli.exe`. Everything that changes data happens in `build()` (`build.cpp`); the plugin and the CLI only set its
options and show its result.

## Plugin start

TW3SE calls `TW3SE_PluginLoad` (`plugin.cpp`) before the game compiles its scripts and before it reads mods. The
plugin finds the game folder, the `Mods` folder and, if it exists, `mods.settings`, runs `build()`, logs one line with the timings,
and registers the console command `itemxl`. A failed build never stops the game; the report says what went wrong.

## The build

```
stamp ─▶ game files ─▶ mods ─▶ rules ─▶ new records ─▶ changes ─▶ texts, appearances ─▶ checks ─▶ write
```

1. **Stamp.** `build()` lists the game's bundles, every mod folder (`content\*.bundle`, `content\*.w3strings`,
   `itemxl\**`) and the standalone YAML files, with sizes and times, plus `mods.settings` if there is one and ItemXL's
   own binary. If the result equals `itemxl.stamp` in the
   output, nothing changed and the build ends. Each folder is listed once; the lists are reused below.
2. **Game files.** Every definition XML of the game's bundles and of `<game>\dlc` is read and parsed into a `DefFile` object each
   (`xml.cpp`), together with the definition files of DLCs inside mods' bundles. A second, untouched copy serves as
   the vanilla reference for imports and checks. `Index` maps (kind, name) to every copy of a record.
3. **Mods.** In load order (`mod_order`): bundles of each mod that replace game files are diffed against the
   vanilla copy (`diff_files`) and become deltas; the mod's YAML files are parsed (`yaml.cpp`) and expanded
   (`expand`: `$instances`, tags to operations). Standalone YAMLs follow.
4. **Rules.** `$requires`, `$after`, and `$if` are resolved now that all mods and all new record names are known;
   `$after` reorders the pending deltas.
5. **New records.** `split_new` takes every `$base`/`$xml` record out of every delta and creates it first
   (`create`), so later changes can find records of any mod. A record whose `$base` is another new record waits
   until that one exists.
6. **Changes.** `apply_delta` applies each delta in order. For a named record it changes the copies `targets` picks
   (the first of each set plus `replace` copies) and then `apply_extends` (fields that `extend` copies set). A
   `$where` record goes through `apply_where`. Every write goes through the `Ledger`, which records conflicts.
7. **Texts and appearances.** `$text` entries become `tw3se\strings\<lang>.txt`; `$appearance` requests are built
   last, when each item has its final `equip_template` (`appearance.cpp`, `cr2w.cpp`, `texture.cpp`).
8. **Checks.** `check_new_items` and `validate` compare what the YAML files named with the game's catalog.
9. **Write.** Changed files (`dirty`) and ItemXL's DLC are packed into `blob0.bundle` (`pack.cpp`), geometry into
   `buffers0.bundle`, textures into `texture.cache`, plus `itemxl.ws` (`script.cpp`) and the report. Everything is
   written into `<output>.tmp` first and copied into the output file by file; stale files of older builds are removed
   only after that succeeded.

## Plugin and command-line tool

| | `ItemXL.dll` | `itemxl-cli.exe` |
|---|---|---|
| entry | `plugin.cpp` | `cli.cpp` |
| build | at game start | `itemxl-cli build` |
| extra | console command, log through TW3SE | `show`, `search`, `new`, `schema` (`browse.cpp`), format helpers for tests |

Only `plugin.cpp` includes the TW3SE header. Everything else builds without the SDK.

## Design rules

- The game's files and scripts are never changed; output goes only into `mod00000_ItemXL`.
- The result depends only on the inputs and their order; the same inputs give the same bytes.
- A broken mod must not break the others: errors are collected, the build goes on.
- What the game does with a file is confirmed in the game before code relies on it
  ([Game behavior](05-game-behavior.md)).

Next: [Source tour](03-source-tour.md).
