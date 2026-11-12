# ItemXL documentation

ItemXL changes and adds Witcher 3 items, abilities, recipes, and loot through YAML files. The pages below explain
the whole framework. Pick the part that matches what you already know.

## For modders

Start here if you want to build mods with ItemXL. The pages build on each other; read them in order the first time.

| Page | What it covers |
|---|---|
| [Getting started](modding/01-getting-started.md) | Your first ItemXL mod: folder, YAML file, game start, report |
| [Concepts](modding/02-concepts.md) | Records, copies, the build at game start, the output mod |
| [YAML format](modding/03-yaml-format.md) | How a file is laid out, attributes, child elements, stats |
| [Operations](modding/04-operations.md) | `!append`, `!remove`, `!scale`, `!offset` and the other operations |
| [New items](modding/05-new-items.md) | `$base`, `$instances`, `$text`, merchants, loot, recipes, alchemy |
| [Rules](modding/06-rules.md) | `$where`, `$if`, `$requires`, `$after` |
| [Appearance](modding/07-appearance.md) | Own models, textures, templates, dyes, and icons |
| [Load order and compatibility](modding/08-load-order-and-compatibility.md) | Conflicts, whole-file mods, XML override, script mods |
| [Report and console](modding/09-report-and-console.md) | Reading the report, notices in the game, console commands |
| [Command-line tool](modding/10-cli.md) | `itemxl-cli`: look up records, start a YAML file, check a mod |
| [VS Code](modding/11-vscode.md) | Completion and checks while you type |
| [Scripts](modding/12-scripts.md) | Using ItemXL items from WitcherScript |
| [Troubleshooting](modding/13-troubleshooting.md) | Common problems and what causes them |
| [Examples](modding/14-examples.md) | The example mods in this repository |
| [AI tools](modding/15-ai-tools.md) | Writing mods with AI assistants: what to give them, how to check the result |

## For TweakXL and ArchiveXL users

For modders who know the Cyberpunk 2077 frameworks. Read [Getting started](modding/01-getting-started.md) first.

| Page | What it covers |
|---|---|
| [Witcher 3 for Cyberpunk modders](tweakxl-archivexl/01-witcher3-for-cyberpunk-modders.md) | Folders, load order, data, scripts, and tools of The Witcher 3 |
| [From TweakXL](tweakxl-archivexl/02-from-tweakxl.md) | TweakDB records and flats compared with Witcher 3 definition records |
| [From ArchiveXL](tweakxl-archivexl/03-from-archivexl.md) | `.xl` files, factories, and appearances compared with `$appearance` |

## For contributors

How ItemXL works inside, for anyone who wants to change or improve the code.

| Page | What it covers |
|---|---|
| [Building and testing](contributing/01-building-and-testing.md) | Requirements, build, test suite, test cases |
| [Architecture](contributing/02-architecture.md) | The build from game start to output, step by step |
| [Source tour](contributing/03-source-tour.md) | Every source file and its main functions |
| [File formats](contributing/04-file-formats.md) | Bundles, CR2W, textures, DLC definitions, strings |
| [Game behavior](contributing/05-game-behavior.md) | What was measured in the game and what the code relies on |
| [Guidelines](contributing/06-guidelines.md) | Conventions for changes and pull requests |

## Outside this documentation

- Making models, templates, and other game resources: the
  [Witcher 3 REDkit documentation](https://cdprojektred.atlassian.net/wiki/spaces/W3REDkit) by CD PROJEKT RED.
- The script extender ItemXL runs on: [TW3SE](https://www.nexusmods.com/witcher3/mods/13837).
- Witcher 3 resources with community tools: [WolvenKit 7](https://github.com/WolvenKit/WolvenKit-7-nightly).
