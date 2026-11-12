# From ArchiveXL

ArchiveXL lets a Cyberpunk 2077 mod add items with their own look without replacing game files. ItemXL's
[`$appearance`](../modding/07-appearance.md) does this for The Witcher 3. The resources differ; the approach is the
same: your files under paths of your own, and a framework that registers them.

## What corresponds to what

| Cyberpunk 2077 / ArchiveXL | The Witcher 3 / ItemXL |
|---|---|
| `.xl` file | the `$appearance` block of the item in its YAML |
| `.archive` with your resources | `Mods\<mod>\itemxl\files\` with your resources under their depot paths; ItemXL packs them |
| factory (`.csv`) registering an entity | ItemXL mounts its templates folder in its own DLC (`.reddlc` with a templates mounter) |
| `.ent` / `.app` (entity, appearances) | entity template `.w2ent`, holding appearances and including a `_meshes` template |
| `.mesh` | `.w2mesh` plus its geometry in a `.1.buffer` file |
| `.xbm` texture | `.xbm` too, but its pixels live in a `texture.cache`; you bring a `.dds`, ItemXL makes both |
| `appearanceName` on the item record | `equip_template` on the item; ItemXL sets it to the copy it makes |
| icon atlas (`.inkatlas`) | one `.dds` per item, set with `icon` |
| localization `.json` | `$text` on the record |

## Two ways, as in ArchiveXL

- **Reuse the game's setup, swap the files.** Name a game template with `from` and map the paths it uses to yours
  with `replace`. ItemXL copies the template and the templates it includes and points the copies at your files.
- **Bring your own template.** Name your own `.w2ent` with `from`. ItemXL mounts it as it is.

Details and options: [Appearance](../modding/07-appearance.md).

## Not available in The Witcher 3

- **Dynamic appearances** (ArchiveXL's substitutions for body type, gender, and other conditions): the game dresses
  only Geralt. The one variation armor needs, "gloves worn", is set with `gloves`.
- **Resource patching** of a game resource in place: ItemXL makes copies instead and points the item at them.
- **Garment support layering**: an armor in Witcher 3 is one template per item.

## Differences in the resources

- Geometry in a `.buffer` file is only read from a bundle named `buffers0.bundle`; ItemXL puts it there. Without it
  the model is invisible.
- Dyes are appearances inside the template (`dye_*`), available for armors of quality 5.
- Texture groups (`CharacterDiffuse`, `WorldDiffuse`, …) steer streaming and downscaling, as in RED4.

Making the resources themselves is outside ItemXL; see the
[Witcher 3 REDkit documentation](https://cdprojektred.atlassian.net/wiki/spaces/W3REDkit) and
[WolvenKit 7](https://github.com/WolvenKit/WolvenKit-7-nightly).
