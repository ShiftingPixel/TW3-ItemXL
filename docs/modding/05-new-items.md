# New items

A new record needs no DLC folder of your own. ItemXL creates it in its own DLC (`dlcitemxl`) inside the output mod,
together with the copy for New Game Plus.

## `$base`: a copy of an existing record

```yaml
item:
  My Steel Sword:
    $base: Long Steel Sword   # starts as an exact copy
    price: 250                # then your changes
    enhancement_slots: 2
```

The copy keeps everything of the original: category, model (`equip_template`), abilities, tags. Change what should
differ. If a record of that name exists already, it is not new: `$base` is ignored and the fields change the existing
record.

`$base` copies the record as the **game files** have it, not as other mods changed it. If a mod gives the original
more rune slots, your copy does not get them. Set the value yourself, or use [`$if`](06-rules.md#if) to follow such a
mod only when it is installed.

`$base` can also name a new record of another mod. That record is created first, whatever the load order of the two
mods; name the other mod in [`$requires`](06-rules.md#requires).

Abilities work the same way:

```yaml
ability:
  My Steel Sword _Stats:
    $base: Long Steel Sword _Stats
    SlashingDamage: !scale 1.2
item:
  My Steel Sword:
    $base: Long Steel Sword
    base_abilities:
      - !remove Long Steel Sword _Stats
      - !append My Steel Sword _Stats
```

## `$xml`: a record written by hand

```yaml
item:
  My Junk:
    $xml: '<item name="My Junk" category="junk" price="3" weight="0.1" stackable="10"/>'
```

ItemXL puts it into its own DLC. With `$file: gameplay\items\def_item_junk.xml` the record goes into that game file
instead (rarely needed).

## `$instances`: many records from one

As in TweakXL, `$(key)` in names and values is filled in once per entry:

```yaml
item:
  My Bolt $(kind):
    $base: Bodkin Bolt
    $instances:
      - {kind: Fire, price: 40}
      - {kind: Frost, price: 45}
    price: $(price)
```

This makes `My Bolt Fire` and `My Bolt Frost`.

## `$text`: name and description

```yaml
item:
  My Steel Sword:
    $text:
      name: {en: My Steel Sword, de: Mein Stahlschwert}
      description: {en: A plain sword with two rune slots.}
```

The game languages are `ar br cn cz de en es esmx fr hu it jp kr pl ru tr ua zh`. A language you leave out shows the
English text. A text without any language code counts as English and is therefore shown in every language:

```yaml
    $text:
      name: My Steel Sword          # the same as {en: My Steel Sword}
```

ItemXL writes the texts through TW3SE, so no `.w3strings` file is needed.

### Texts of existing records and translations

`$text` works on any record, not only new ones, and name and description behave the same. On a record that has
texts already (from the game or another mod), only the languages you write change; every other language keeps the
text it had. A language you write replaces what the record had in it, so a translation mod can also replace a weaker
translation, for example one made with an AI tool. Say `My Steel Sword` from above comes from another mod and has
only English and German:

```yaml
item:
  My Steel Sword:
    $text:
      name: {zh: 我的钢剑, de: Mein stählernes Schwert}         # Chinese new, German replaced, English stays
      description: {zh: 一把有两个符文槽的普通剑。}
```

Several translation mods for the same record combine: one can bring Chinese, another Polish. If two write the same
language, the one later in [load order](08-load-order-and-compatibility.md) wins.

This differs from the first `$text` of a record: a record that had no text before shows the English text in every
language you leave out.

### Adding to an existing description

`!append` adds a line at the end of the description the item already has, instead of replacing it:

```yaml
item:
  Long Steel Sword:
    $text:
      description: !append {en: Every third hit deals double damage., de: Jeder dritte Treffer verursacht doppelten Schaden.}
```

The line goes into every language: each language keeps its own description, and the line follows it in a new line
(ItemXL puts the `<br>` in between). A language you leave out gets the English line.

Another mod translates the line the same way as above: it writes only its languages and leaves out `en`. An `!append`
without `en` adds no line; its languages replace those of the appended line:

```yaml
item:
  Long Steel Sword:
    $text:
      description: !append {de: Jeder dritte Treffer richtet doppelten Schaden an., zh: 每第三次命中造成双倍伤害。}
```

As with names, the mod later in load order wins when two write the same language. If the record has no appended line with English, the report warns and the translation is ignored. When several mods
appended lines to the same description, translations go to the first of these lines.

Descriptions understand a few HTML-like tags. `<br>` starts a new line. `<font color='#RRGGBB'>…</font>` gives the
text inside another colour: `#RRGGBB` is a colour in hexadecimal, red, green, and blue from `00` to `FF`, as in HTML
and CSS (`#FF0000` red, `#00FF00` green, `#FFFFFF` white). Text outside a `<font>` tag has the tooltip's own colour.

```yaml
      description: !append {en: "<font color='#FF6060'>Every third hit deals double damage.</font>"}
```

## Merchants, loot, and recipes

Three short forms create the records an item needs to appear in the game:

```yaml
item:
  My Steel Sword:
    $base: Long Steel Sword
    $sell: [_store__Hattori_Master_Blacksmith]    # merchants that sell it
    $loot: {<loot list>: {quantity_max: 2}}       # loot lists it can drop from
    $recipe:                                      # a crafting diagram
      crafter: Smith                              # Smith, Armorer, Crafter
      level: Master                               # Journeyman, Master, Grand Master, Arch Master
      rarity: Master                              # quality of the diagram itself, see below
      price: 120                                  # what the craftsman takes
      quantity: 1                                 # how many items one crafting makes (default 1)
      diagram_price: 10                           # price of the diagram itself (default 10)
      ingredients: {Steel ingot: 2, Leather straps: 1}
      sell: [_store__Hattori_Master_Blacksmith]   # who sells the diagram
```

- `$sell` and `$loot` take a list of loot list names, or a mapping from names to entry fields. They always **add** an
  entry for your item; the rest of the list stays as it is, so they cannot wipe a merchant's or enemy's loot.
- Each entry gives one item unless you set `quantity_min` and `quantity_max`: with `quantity_max: 2` that entry gives
  one or two. Other fields of the game's loot entries (`chance`, `player_level_min`, `player_level_max`) can be set
  the same way; look at a list with [`itemxl-cli show <list> --tag loot`](10-cli.md#show) to see what the game uses
  there. How the game uses `chance` is explained [below](#how-the-game-picks-loot).
- `$recipe` creates the diagram `<name> schematic` and its crafting record.
- The diagram's name is the game's word for a diagram plus the item's name, in every language: `Schema: …` in
  German, `Diagram: …` in English. It follows the item's name as it is after all mods, so a translation of the item
  also translates its diagram.
- `rarity` is the quality of the diagram item, not of the crafted item. ItemXL gives the diagram the game's quality
  ability `<rarity> _Stats`, as the game's own diagrams have it; the inventory shows it as the colour of the name.
  Allowed, from low to high: `Master`, `Magic` (the default), `Relic`, `SetItem` (the quality of the witcher school gear).
- Merchant and loot list names are found with [`itemxl-cli search _store_ --tag loot`](10-cli.md#search).

### Alchemy recipes

`$alchemy` does for potions, bombs, oils, and decoctions what `$recipe` does for crafted items: it creates the
recipe and the recipe item the player reads to learn it, both named `<name> recipe`.

```yaml
item:
  My Potion:
    $base: Swallow 1                              # the potion itself: a copy of Swallow
    $text:
      name: {en: My Potion, de: Mein Trank}
    $alchemy:
      ingredients: {Celandine: 1, Dwarven spirit: 1}
      price: 15                                   # price of the recipe item
      sell: [_store__Hattori_Master_Blacksmith]   # who sells the recipe
```

The new recipe follows the game's recipe for the `$base` item: it takes over what kind of brew it is (potion, bomb,
oil, decoction) and the look of its recipe item. For an item without a game recipe, name one with `like:`
(`like: Recipe for Grapeshot 1`).

The recipe item's name is built like the game's: the word the game's own recipes of that kind start with, and the
item's name, in every language: `Manuscript page: My Potion`, `Manuskriptseite: Mein Trank`. Potions, bombs, and oils
get `Manuscript page:`, decoctions `Torn-out page:`, substances and dyes `Formula:`. Like a diagram's name, it follows
the item's name after all mods.

| Field | Meaning |
|---|---|
| `ingredients` | the ingredients and their amounts |
| `level` | the recipe's level (default 1) |
| `group` | recipes of one group are levels of one brew: brewing a higher level takes the lower ones out of the inventory. By default every new recipe is a group of its own |
| `price`, `rarity` | price and quality of the recipe item, as for diagrams |
| `like` | the game's recipe to follow (default: the recipe of `$base`) |
| `sell` | merchants that sell the recipe item |

Potions, bombs, oils, and decoctions are single items: the player has each one once. The game brews one only while
the player does not have it yet, nor a higher level of it (an item whose name starts the same, such as `Swallow 2`
for `Swallow 1`). How often it can be used is its **`ammo`** stat, in one of its abilities (Swallow 3, Grapeshot 2,
Beast Oil 20, decoctions 1); meditating refills it. To change the uses, give the item a copy of that ability:

```yaml
ability:
  My Potion _Stats:
    $base: Swallow_1
    ammo: {min: 5}                         # 5 uses instead of 3
item:
  My Potion:
    base_abilities:
      - !remove Swallow_1
      - !append My Potion _Stats
```

### How the game picks loot

A loot list hands out a few of its entries, not each entry on its own. This is how the game does it (read from the
game's code):

- **How many entries:** a random number between the list's own `quantity_min` and `quantity_max` (the attributes of
  the `<loot>` element, not of the entries). The drowners' list has 0 and 2: a drowner gives zero, one, or two
  different things.
- **Which entries:** entries with `chance="-1"` always come first. The rest are drawn one by one, and `chance` is a
  **weight**, not a percentage: an entry with chance 50 next to entries that add up to 150 is drawn in about a quarter
  of the draws. An entry is drawn only once per hand-out, and the order of the entries does not matter. An entry
  that `$sell` or `$loot` adds without a `chance` gets `-1`; it comes first, but only when the list hands out at
  least one entry.
- **Small lists:** if the list has no more entries than it hands out, every entry with a chance above 0 is given,
  whatever its chance.
- **How many of each:** the entry's own `quantity_min` and `quantity_max`.

Entries whose `player_level_min`/`player_level_max` do not fit the player's level are left out first (0 means no
limit). So for an item to drop more often, raise its `chance` compared to the others in the list; to give more items
per enemy, raise the list's `quantity_max`:

```yaml
loot:
  _monster__Drowner:
    quantity_max: 3            # the list itself: up to three things per drowner
```

## New Game Plus

Every new record is created in both sets, the normal game and New Game Plus. The New Game Plus copy is made from the
New Game Plus copy of the `$base` record, so it gets what the game has there (for example its higher item levels).
Your changes go to both copies.

## Checks for new items

The report warns when a new item lacks something the game needs: a category, a name text, an icon, or a look
(`equip_template`) for things that can be worn or held. A `$base` copy has all of them already. Items with the tag
`NoShow` never appear in the inventory (scabbards and other technical items), so they need no icon or name text.

Next: [Rules](06-rules.md).
