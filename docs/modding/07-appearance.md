# Appearance

This is the part of ItemXL that does what ArchiveXL does for Cyberpunk 2077: an item that looks like files of its
own. ItemXL packs and wires your files; it does not make models or textures. Those come from the
[Witcher 3 REDkit](https://cdprojektred.atlassian.net/wiki/spaces/W3REDkit) or
[WolvenKit 7](https://github.com/WolvenKit/WolvenKit-7-nightly).

## How an item looks in Witcher 3

An item's `equip_template` names an **entity template** (`.w2ent`). For armor, this template holds the appearances
(including the `dye_*` ones for dyeing) and includes a second template, the `_meshes` one, which holds the model
(`.w2mesh`), cloth, and dangling parts. Models keep their geometry in a separate `.buffer` file; textures are `.xbm`
files whose pixels live in a `texture.cache`.

Many armors have a second template for "gloves worn" (sleeves tucked in), used through a `<variant>` of the item.

## Your files

Put your files into the mod under their **depot paths**, the paths the game will know them by:

```
Mods\modMyArmor\itemxl\
  my_armor.yaml
  files\
    mymod\armor\my_armor.w2mesh
    mymod\armor\my_armor.w2mesh.1.buffer
    mymod\armor\my_armor_d01.dds
    mymod\icons\my_armor.dds
```

Two kinds of files are left out with a warning: a file under the path of a definition file ItemXL merges (change
its records in YAML instead), and a file whose depot path is longer than 255 characters (a bundle cannot hold it).

A `.dds` becomes a `.xbm` of the same path, with its pixels in ItemXL's `texture.cache`. Use a compressed format
(DXT1, DXT5, BC4, BC5, BC7) with mipmaps; the report warns about uncompressed textures, missing mipmaps, and DXT5
textures without any transparency (DXT1 would be half the size).

## Way 1: build on an existing template

```yaml
item:
  My Armor:
    $base: Gryphon Armor 4          # values, set bonus, slots
    $appearance:
      from: t_01_mg__wolf_lvl5      # the template to copy
      gloves: t_01a_mg__wolf_lvl5   # the one for "gloves worn"
      replace:                      # what the copies use -> your files
        dlc\bob\data\characters\models\geralt\armor\armor__wolf\t_05_mg__wolf.w2mesh: mymod\armor\my_armor.w2mesh
```

A template rarely stands alone: an armor's template includes its `_meshes` template, which names the model. ItemXL
copies all of them, changes the paths in the copies as `replace` says, and puts the copies into its DLC
(`dlc\dlcitemxl\data\items\itemxl\itemxl_<item>.w2ent`). Then it sets the item's `equip_template` to the copy. The
"gloves worn" variant gets its own copy, otherwise the old look would show when gloves are worn.

`replace` maps any path the template uses (models, textures, materials) to yours. Find the paths with WolvenKit or
REDkit.

## Way 2: your own template

```yaml
item:
  My Armor:
    $base: Gryphon Armor 4
    $appearance:
      from: mymod\armor\my_armor.w2ent       # your template in itemxl\files\
      gloves: mymod\armor\my_armor.w2ent
```

ItemXL mounts it in its DLC and uses it as is. Every file it uses must exist in the game or in a mod's
`itemxl\files\`; a missing file is an error in the report.

To make your own template from one of the game's, `itemxl-cli retarget` writes a copy that points elsewhere (see
[Command-line tool](10-cli.md#retarget)).

## Options

| Key | Effect |
|---|---|
| `from` | template to copy: a name (`t_01_mg__wolf_lvl5`) or a depot path; default: the item's own `equip_template` |
| `gloves` | template for "gloves worn"; default: `from` |
| `replace` | paths the copies use → your files |
| `colors: off` | drop the template's colour shift |
| `groups` | texture group per `.dds`, see below |
| `icon` | inventory icon, see below |

`$appearance` with only `icon` gives an item its own icon and keeps its look.

### Texture groups

Every texture belongs to a texture group. The group decides how the game streams the texture and how far it may
shrink it at lower texture settings; it does not change the picture. By default ItemXL picks the group the game uses
for that format on characters (`CharacterDiffuse` for DXT1, `CharacterNormalmapGloss` for normal maps, …). Geralt's
armors use both `Character*` and `World*` groups. To match a texture you replace, set its group:

```yaml
      groups:
        mymod\armor\my_armor_d01.dds: WorldDiffuse
```

### Icons

An icon is a `.dds` in DXT5. Its shape follows the item: an item two cells high in the inventory (`grid_size: 2`,
weapons and armor) has an icon twice as high as wide, like the game's 64×128; every other item has a square icon,
64×64. The inventory scales an icon to the item's cells, so another size of the right shape works as well, for
example 32×64 or 128×256; a larger one only costs memory. An icon of the wrong shape is reported as a warning.

## Dyes

The game lets an armor be dyed when its quality is 5 and it does not have the tag `noDye`. Dyeing switches to the
template's `dye_*` appearances, so the template needs them. Copies made with way 1 keep the dye appearances of the
template they copy.

If your armor has quality 5 but its template has no dye appearances, dyes would do nothing. The report warns about
it. Give the item the tag `noDye`, and the game no longer offers dyeing for it:

```yaml
item:
  My Armor:
    tags: !append [noDye]
```

## Several mods, same file

When two mods bring a file with the same depot path, the later one in the load order wins, and the report says so.
Use a folder of your own (`mymod\…`) to avoid that.

Next: [Load order and compatibility](08-load-order-and-compatibility.md).
