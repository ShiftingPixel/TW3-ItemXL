# File formats

The formats ItemXL reads and writes, in short. The full byte layouts are in the comment at the top of each source
file; this page says what each format is for and where the code is.

## Bundles (`bundle.cpp`, `pack.cpp`)

A mod's or the game's `content\*.bundle` is a container of files under depot paths (`gameplay\items\x.xml`).
Header `POTATO70`, then a table with one entry per file. Two entry layouts exist: 0x130 bytes, used by every bundle of the
game itself (5.x), and 0x140 bytes, an older layout. Both occur in mods: in a list of 184 mod
bundles, 77 used 0x130 and 107 used 0x140. The table does not say which one it uses; the reader takes the layout
whose entries all fit inside the file. Files are stored plain, with
zlib, or with LZ4 (definition XMLs use zlib or LZ4).

ItemXL writes `blob0.bundle` uncompressed, each file 16-byte aligned. Model geometry (`.buffer` files) has to go
into `buffers0.bundle`; the game reads geometry only from there.

`metadata.store` lists every file of a mod's bundles with paths, sizes, directories, and FNV-1a 64 hashes of the
lower-case paths. The game uses it to find files; ItemXL's writer matches the reference tool byte for byte.

## Definition XML (`xml.cpp`, `model.cpp`)

`<redxml><definitions>` with groups such as `<items>`, `<abilities>`, `<loot_definitions>`, `<schematics>`. A record
is an element with a key attribute (`name`, or `name_name` for schematics). The game's files contain a few quirks the
reader accepts: comments with `--` inside, duplicate attributes (the first is kept, as the game does), UTF-16 files
from older mods, and Unicode spaces in whitespace.

The writer lays files out like Python's ElementTree with tab indentation, so unchanged records stay recognisable and
the output is stable from build to build.

## CR2W resources (`cr2w.cpp`)

`.w2ent`, `.w2mesh`, `.xbm`, `.reddlc`, and most other resources are CR2W files: a header, tables (strings, names,
imports, properties, exports, buffers), and the serialized objects. ItemXL reads them, changes import paths and some
properties, and writes them again with all offsets and checksums recomputed. An entity template also keeps an
embedded, compiled copy of its components (`flatCompiledData`); `cr2w_retarget` changes the paths there as well.

## Textures (`texture.cpp`)

A texture is an `.xbm` resource plus pixel data in a `texture.cache`. The `.xbm` holds the small mips itself; the
larger ones are read from the cache, found by a 32-bit key. ItemXL turns a `.dds` into both. The key of a cache
entry is the hash of its depot path (`h = h * 31 + c` over the lower-case path); the game looks icons up this way,
and every entry in the game's caches follows it. The cache is version 7, the version of the game's own `texture.cache` in 5.x, data in 4096-byte pages, one zlib piece per
large mip and the small mips together in the last piece.

Compression and texture group per format follow what the game uses for its own textures (DXT1 diffuse
`CharacterDiffuse`, DXT5 normal maps with gloss `CharacterNormalmapGloss`, BC7 `QualityColor`, …).

## DLC definitions (`reddlc.cpp`)

A `.reddlc` is a CR2W file with a `CDLCDefinition` and mounters. `CR4DefinitionsDLCMounter` mounts definition XMLs
(a file, or a folder ending in `\`), `CR4DefinitionsNGPlusDLCMounter` the New Game Plus ones,
`CR4DefinitionsEntitieTemplatesDLCMounter` a folder of item templates. The game finds a `.reddlc` in any bundle,
also in a mod's bundle in `Mods\`. ItemXL's own DLC is `dlcitemxl`.

## Texts

The `.w3strings` files of the game, its DLCs, and the mods (`w3strings.cpp`) are read only: to reuse the game's own
words (for example "Diagram" in every language) and to keep the texts a `$text` leaves out. ItemXL's texts go to
`tw3se\strings\<lang>.txt`, which TW3SE loads, so no `.w3strings` file has to be written.

A file holds its texts either as UTF-16 units (the original game, version 162) or as UTF-8 bytes (Remastered, 164).
The length of the text block tells which: some mods' files carry version 162 but the UTF-8 layout.

## Scripts (`script.cpp`)

`itemxl.ws` is plain WitcherScript using `@addField`, `@wrapMethod`, and `@addMethod`. ItemXL writes it at every
build into the output mod; it is not a file of this repository.

Next: [Game behavior](05-game-behavior.md).
