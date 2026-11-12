# Source tour

All sources are in `plugin/src/`. `itemxl.hpp` declares the core and is the best place to start reading; every
`.cpp` file opens with a comment on what it does and the formats it handles.

## Entry points

| File | Content |
|---|---|
| `plugin.cpp` | the TW3SE plugin: `TW3SE_PluginQuery`, `TW3SE_PluginLoad` (finds the folders, runs the build, logs), the console command `itemxl` |
| `cli.cpp` | `itemxl-cli`: argument parsing and one block per command; the test helpers (`unpack`, `pack`, `extract`, `roundtrip`, `yaml`, `cr2w`, `xbm`, `reddlc`) live here too |
| `version.hpp` | the version, the only place it is set |

## The build

| File | Content |
|---|---|
| `build.cpp` | `build()`: stamp, reading the game and the mods, rules (`$requires`, `$after`, `$if`), the two passes, texts, appearances, checks, report, output. `mod_order` reads `mods.settings`, `scan_files` lists a folder once for stamp and build. |
| `model.cpp` | records and deltas. `Index` (kind, name → copies), `targets` (copies a change goes to), `apply_delta`, `apply_record` (one record: attributes, lists, stats, operations), `apply_where` and `where_match` (`$where`), `apply_extends` (`extend` copies), `split_new` and `create` (new records), `resolve_from` (`!append-from`), `expand` (`$instances`, YAML tags), `shortcuts` (`$sell`, `$loot`, `$recipe`, `$alchemy`), `take_texts` and `append_texts` (`$text`), `diagram_names` and `alchemy_names` (names of diagrams and recipe items), `diff_files` (whole-file imports), `Ledger` (conflicts), `Catalog`, `validate`, `check_new_items` |
| `appearance.cpp` | `$appearance`: `take_appearances` collects the requests, `build_appearances` copies and retargets templates, packs textures and icons, checks files and dyes, adds the DDS warnings |
| `script.cpp` | `itemxl_script`: the generated `itemxl.ws` |

## Formats

| File | Content |
|---|---|
| `bundle.cpp` | reading `.bundle` files (both entry layouts) and their compressions (zlib, LZ4); `is_definition_xml` |
| `pack.cpp` | writing `blob0.bundle`, `buffers0.bundle`, and `metadata.store` (`write_content`) |
| `xml.cpp` | a small XML DOM (`Node`, `xml_parse`, `xml_to_string`, `xml_indent`) that keeps the game's layout; UTF-16 input, depth limit |
| `yaml.cpp` | the YAML subset ItemXL uses (`yaml_load_all`), read like PyYAML's SafeLoader: anchors, merge keys, tags, flow style; limits against hostile input |
| `cr2w.cpp` | CR2W resources (`.w2ent`, `.w2mesh`, `.xbm`): read, change imports and properties, write; `cr2w_retarget` |
| `texture.cpp` | DDS → `.xbm` and `texture.cache` (`read_dds`, `texture_pieces`, `make_xbm`, `write_texture_cache`, `texture_key`) |
| `reddlc.cpp` | `.reddlc` DLC definitions (`build_reddlc`, `itemxl_reddlc`) |
| `w3strings.cpp` | reading the game's `.w3strings` texts; `diagram_prefix` finds the game's word for "diagram" per language |

## Tools for authors

| File | Content |
|---|---|
| `browse.cpp`, `browse.hpp` | `GameData` (the game's records and texts), `show`, `search`, `schema` (the VS Code schema), `new_yaml` |

## Helpers

| File | Content |
|---|---|
| `util.cpp` | strings (`lower`, `trim` with Unicode spaces, `split`), hashes (`fnv1a64`, `crc32`), files (`read_file`, `write_file`, `make_dirs`), number formatting (`fmt_double`) |

## Outside `plugin/src`

| Path | Content |
|---|---|
| `plugin/build.bat` | the build with Zig |
| `tests/` | the test suite ([Building and testing](01-building-and-testing.md)) |
| `tools/paths.py` | finds the game and the work folder for tests and tools |
| `tools/make_look_example.py` | builds the appearance example from the installed game |
| `examples/` | the example mods ([Examples](../modding/14-examples.md)) |

Next: [File formats](04-file-formats.md).
