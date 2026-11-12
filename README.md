# ItemXL

ItemXL is a modding framework for The Witcher 3. It lets mods add items and make targeted changes to items, abilities, recipes, and loot through YAML files. ItemXL combines those changes when the game starts, so mods do not need to replace entire game definition files just to change a few values.

ItemXL runs as a plugin for the [Witcher 3 Script Extender (TW3SE)](https://www.nexusmods.com/witcher3/mods/13837). It can also package an item's custom model, textures, and icon when the mod author supplies the required game resources.

## Download and installation

### Players

Download the compiled ItemXL plugin from [Nexus Mods](https://www.nexusmods.com/games/witcher3/mods/15642) and follow the installation instructions on that page.

### Mod authors and developers

To get the source code, open this GitHub repository's **Code** menu and choose **Download ZIP**. Extract the source, then follow [Building and testing](docs/contributing/01-building-and-testing.md) to install the build requirements and compile ItemXL with `plugin\build.bat`.

The source ZIP does not contain the compiled plugin or the TW3SE SDK. The SDK folder must be placed in `third_party\TW3SE-SDK` before building. You do not need Git to download or build the source this way.

## What ItemXL does

- Changes existing items, abilities, schematics, and loot records by individual fields.
- Adds new items, including New Game Plus records, merchants, loot, crafting diagrams, and alchemy recipes.
- Applies rules: changes for every matching record, conditions on installed mods, dependencies, and order.
- Supports items with custom models, textures, templates, and icons.
- Imports mods that replace whole definition files and applies their changes alongside YAML mods.
- Reports conflicts, invalid references, and missing items.
- Includes a command-line tool and a VS Code schema for looking up game records and writing YAML.

ItemXL writes its result into its own output mod. It does not modify the installed game files.

## Example

```yaml
item:
  Bodkin Bolt:
    price: 5
    tags: !append [mod_legendary]

  My Steel Sword:
    $base: Long Steel Sword
    enhancement_slots: 2
    $text:
      name: {en: My Steel Sword, de: Mein Stahlschwert}
    $sell: [_store__Hattori_Master_Blacksmith]

  Every crossbow:
    $where: {category: crossbow}
    price: !scale 1.2
```

## Documentation

The [documentation](docs/README.md) has three parts:

- **For modders:** from [your first mod](docs/modding/01-getting-started.md) to rules, custom appearances, and troubleshooting.
- **For TweakXL and ArchiveXL users:** how the Cyberpunk 2077 frameworks [map to ItemXL](docs/tweakxl-archivexl/01-witcher3-for-cyberpunk-modders.md).
- **For contributors:** [building and testing](docs/contributing/01-building-and-testing.md), architecture, and file formats.

## Limitations

- ItemXL does not create models or textures. It packs the ones a mod brings; see the [Witcher 3 REDkit documentation](https://cdprojektred.atlassian.net/wiki/spaces/W3REDkit) for making them.
- Changes take effect at the next game start; records are not changed while the game runs.
- Values that scripts change at runtime are outside ItemXL's merge and report.
- Generated bundle and texture data is stored without compression, which uses additional disk space.

## Use of AI

Parts of ItemXL, its code and documentation, were written with the help of AI tools. The results were checked with the
automated tests and by hand in the game.

## Acknowledgements

ItemXL was inspired by [TweakXL](https://github.com/psiberx/cp2077-tweak-xl) and [ArchiveXL](https://github.com/psiberx/cp2077-archive-xl), two modding frameworks for Cyberpunk 2077.

Thanks to [MaximDeWinter](https://www.nexusmods.com/profile/MaximDeWinter) for creating and maintaining the Witcher 3 Script Extender.
