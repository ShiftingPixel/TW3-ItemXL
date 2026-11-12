# Examples

Complete mods in the [`examples`](../../examples) folder. Copy one into `<game>\Mods\` to try it. Each one is small
and commented line by line; read the YAML next to this list. Most of them sell their items, so you can buy them in
the game and test them.

| Mod | Shows | Where to find it in the game |
|---|---|---|
| [`modItemXLExampleBlade`](../../examples/modItemXLExampleBlade/itemxl/example_blade.yaml) | a new sword from one YAML file: `$base` for item and ability, `!scale`, `!remove`/`!append` in a list, `$text` in two languages, `$sell`, `$recipe` | the sword and its diagram at Hattori, the master smith in Novigrad |
| [`modItemXLExampleArmor`](../../examples/modItemXLExampleArmor/itemxl/example_armor.yaml) | a new armor set: YAML anchors to reuse merchants and recipes, `!offset`, grandmaster crafting | the armor, the gauntlets, and their diagrams at the top-notch swords guy on Hierarch Square in Novigrad |
| [`modItemXLExampleAmmo`](../../examples/modItemXLExampleAmmo/itemxl) | two files, one topic each. [`bolts.yaml`](../../examples/modItemXLExampleAmmo/itemxl/bolts.yaml): `$instances` (two bolts from one entry), a recipe that makes 10 bolts, `rarity`, merchant stacks with `quantity_min`/`quantity_max`. [`trinket.yaml`](../../examples/modItemXLExampleAmmo/itemxl/trinket.yaml): a record written as `$xml`, `$loot` with `chance` | bolts and diagrams at the blacksmiths of Toussaint (Blood and Wine); the charm from drowners |
| [`modItemXLExampleBalance`](../../examples/modItemXLExampleBalance/itemxl/balance.yaml) | changing what the game has: plain values, `!append` on tags, `$where` rules, a counted list, `!scale`/`!offset`/`null` on stats, a change to one copy of a record (only New Game Plus), the size of a loot list itself, several documents with `$if` (also `not` and `any`) | the Witcher's steel sword (`Long Steel Sword`) and candles; crossbow prices at any merchant; drowners drop up to three things |
| [`modItemXLExampleAlchemy`](../../examples/modItemXLExampleAlchemy/itemxl/draught.yaml) | a potion in two levels and a bomb with their recipes: `$alchemy` with `group`, `level`, `rarity`, a new item as an ingredient, more uses through the `ammo` stat | the first recipe at the herbalist of White Orchard, the second at the herbalist of Kaer Trolde on Skellige, the bomb's at the alchemists of Toussaint (Blood and Wine) |
| [`modItemXLThirdHit`](../../examples/modItemXLThirdHit) | YAML and script together: a tag in YAML, the behavior in WitcherScript with annotations; changes the example blade from another mod | needs `modItemXLExampleBlade`; every third hit of that sword deals double damage |
| [`modItemXLExampleTranslation`](../../examples/modItemXLExampleTranslation/itemxl/zh.yaml) | a translation mod for another mod: `$requires`, `$after`, `$text` that adds one language and replaces another, `!append` without `en` | needs `modItemXLExampleBlade`; switch the game to Chinese or German |

The examples fit each other: with all of them installed the report shows no conflicts and no warnings. The test case
`examples` checks that ([Building and testing](../contributing/01-building-and-testing.md)).

The test cases in [`tests/cases`](../../tests/cases) are small mods as well, each for one feature or edge case
(conflicts, XML override, whole-file imports, typos). Their `.json` files say what each one checks.

An example for [custom appearances](07-appearance.md) needs game resources that cannot be shipped in this
repository. `tools/make_look_example.py` builds one from your installed game:

```powershell
python tools\make_look_example.py "<game>"
```

It writes `<game>\Mods\modItemXLExampleLook`: an armor built on the Wolf template with its own model and texture
(red and blue, so it is visibly another texture). It is sold by the top-notch swords guy on Hierarch
Square in Novigrad.

Next: [AI tools](15-ai-tools.md).
