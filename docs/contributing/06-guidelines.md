# Guidelines

## Before a pull request

- `python tests\run_all.py --build` passes.
- A new feature has a test case ([Building and testing](01-building-and-testing.md#test-cases)) and a section in the
  [modding documentation](../README.md#for-modders).
- Anything that depends on how the game loads data was checked in the game, and [Game behavior](05-game-behavior.md)
  says so.

## Code

- C++17, no dependencies beyond the standard library and, for the plugin only, the TW3SE header.
- Each source file starts with a comment on what it does and the formats it handles; keep it current.
- Errors in one mod never stop the build: add them to the ledger's errors or warnings and go on.
- The output depends only on the inputs: no time stamps, random values, or unordered iteration in written files.
- The version is set only in `plugin/src/version.hpp`.

## Text

- Code, comments, documentation, and report messages are in English.
- Report messages name the mod, the file, and the record, and say what happens next ("kept as in the game",
  "this file is skipped").
- Nothing in the repository refers to one machine or one mod setup: no personal paths, no third-party mods in tests
  or examples. Tests make what they need from the installed game.

## Documentation

- Pages follow the order of the [index](../README.md); link related pages where a topic continues.
- Each page shows working YAML. Names in examples are real records of the game or clearly invented ones.
- Making game resources is covered by the [Witcher 3 REDkit documentation](https://cdprojektred.atlassian.net/wiki/spaces/W3REDkit),
  not here.
