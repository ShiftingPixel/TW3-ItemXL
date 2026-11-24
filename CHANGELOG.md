# Changelog

## 1.1.0

- Changes to YAML files take effect while the game runs: ItemXL builds in the background and loads only the changed entries (needs TW3SE 1.4.0 or later)
- New TW3SE console commands: /itemxl reload loads changes at once, /itemxl live on|off turns the watching on or off
- New Game Plus entries are updated while the game runs as well, in a New Game Plus game
- Other mods are told about changes loaded while the game runs on the TW3SE channel itemxl.live
- Items and abilities that a TW3SE Lua mod (w3.items) makes under the same name as ItemXL are reported as a warning
- The notices in the game are shorter and in English only, and name the mods whose changes could not be applied
- Warnings no longer show a notice in the game, only errors do; warnings stay in the report
- Building is faster: the game's files, the mod folders and the texts are read on several threads, and mods are no longer searched for text files they do not have

## 1.0.2

- Files whose path is too long, contains non-ASCII characters or only differs in letter case from another are no longer packed and are reported as a warning

## 1.0.1

- New items with the tag NoShow (e.g. scabbards) are no longer reported for a missing icon or name, only for a missing category
