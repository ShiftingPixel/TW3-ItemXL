"""Paths the tests and tools need. Nothing here belongs to one machine.

Needed by everything:
  GAME        the game folder; found through Steam or GOG, else set it

Set a path in local_paths.json in the project root (not in git), or as ITEMXL_<NAME>, e.g.

  {"GAME": "D:/SteamLibrary/steamapps/common/The Witcher 3"}
"""
import json
import os
import re
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_game() -> str:
    """The game folder from the registry: Steam (its libraries) or GOG. "" when not found."""
    try:
        import winreg
    except ImportError:
        return ""
    found = []
    for key in (r"SOFTWARE\WOW6432Node\Valve\Steam", r"SOFTWARE\Valve\Steam"):
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key) as k:
                steam = winreg.QueryValueEx(k, "InstallPath")[0]
        except OSError:
            continue
        libs = [steam]
        try:
            with open(os.path.join(steam, "steamapps", "libraryfolders.vdf"), encoding="utf-8") as f:
                libs += [p.replace("\\\\", "\\") for p in re.findall(r'"path"\s+"([^"]+)"', f.read())]
        except OSError:
            pass
        found += [os.path.join(lib, "steamapps", "common", "The Witcher 3") for lib in libs]
    for gog_id in ("1495134320", "1207664663"):   # Game of the Year Edition, base game
        try:
            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\GOG.com\Games\\" + gog_id) as k:
                found.append(winreg.QueryValueEx(k, "path")[0])
        except OSError:
            pass
    for p in found:
        if os.path.isfile(os.path.join(p, "bin", "x64_dx12", "witcher3.exe")) or os.path.isdir(os.path.join(p, "content")):
            return p
    return ""


P = {
    "GAME": "",
    "TW3SE_SDK": os.path.join(ROOT, "third_party", "TW3SE-SDK"),   # not in git (third-party code)
    "WORK": os.path.join(tempfile.gettempdir(), "itemxl-tests"),   # the tests' work folder
}
_local = os.path.join(ROOT, "local_paths.json")
if os.path.isfile(_local):
    with open(_local, encoding="utf-8") as _f:
        P.update(json.load(_f))
for _k in P:
    P[_k] = os.environ.get("ITEMXL_" + _k, P[_k])

GAME = P["GAME"] or find_game()
TW3SE_SDK = P["TW3SE_SDK"]
WORK = P["WORK"]
CLI = os.path.join(ROOT, "plugin", "out", "itemxl-cli.exe")
DLL = os.path.join(ROOT, "plugin", "out", "ItemXL.dll")
