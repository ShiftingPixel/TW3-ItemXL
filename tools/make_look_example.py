"""Makes the $appearance example mod: an armor with a model and a texture of its own, the way a mod author ships it.

There is no own model in the repository (game files may not be shipped), so this stands in for one, built from the
game's files: the grandmaster Wolf armor's model under a new path (mymod\\look\\look_armor.w2mesh + .1.buffer) whose
diffuse texture is a new one (mymod\\look\\look_armor_d01, as a DDS with red and blue swapped, so it is visibly
another texture). ItemXL does the rest from the YAML: templates, DLC, texture.cache.

    python tools\\make_look_example.py <mod folder>      writes Mods\\modItemXLExampleLook\\...

In the game: "ItemXL Example Look" at the armorer on Hierarch Square: the Wolf armor's shape, red and blue swapped.
"""
import os
import struct
import subprocess
import sys
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "third_party", "TW3SE-SDK", "tools"))
from cr2w import Depot, CR2W  # noqa: E402
import texcache  # noqa: E402
from tools import paths  # noqa: E402

MOD = "modItemXLExampleLook"
SRC_MESH = "dlc\\bob\\data\\characters\\models\\geralt\\armor\\armor__wolf\\t_05_mg__wolf.w2mesh"
SRC_TEX = "dlc\\bob\\data\\characters\\models\\geralt\\armor\\armor__wolf\\t_05_mg__wolf_d01.xbm"
MESH = "mymod\\look\\look_armor.w2mesh"
TEX = "mymod\\look\\look_armor_d01.xbm"
WOLF = "dlc\\bob\\data\\items\\bodyparts\\geralt_items\\trunk\\witcher_wolf\\t_01_mg__wolf_lvl5.w2ent"
WOLF_MESHES = "dlc\\bob\\data\\characters\\models\\geralt\\armor\\armor__wolf\\t_01_mg__wolf_lvl5_meshes.w2ent"
OWN = "mymod\\own\\own_armor.w2ent"
OWN_MESHES = "mymod\\own\\own_armor_meshes.w2ent"
ICON = "dlc\\bob\\data\\gameplay\\gui_new\\icons\\inventory\\armors\\wolf_armor_lvl5_64x128.png"
ICON_CACHE = ""

YAML = r"""# ItemXL example: an armor that looks like files of its own ($appearance, like ArchiveXL).
# The model and texture are in itemxl\files\ under their depot paths: a .w2mesh with its .1.buffer (made with
# WolvenKit for a real mod), the texture as a .dds (DXT1, DXT5, BC4, BC5 or BC7). ItemXL makes the templates, the
# .xbm, the texture.cache and its DLC from that.
item:
  ItemXL Example Look:
    $base: Gryphon Armor 4                 # values, set bonus, slots
    $appearance:
      from: t_01_mg__wolf_lvl5             # the template to copy: how the parts are put together
      gloves: t_01a_mg__wolf_lvl5          # the one for "gloves worn" (sleeves tucked in)
      replace:                             # the template's model -> ours
        dlc\bob\data\characters\models\geralt\armor\armor__wolf\t_05_mg__wolf.w2mesh: mymod\look\look_armor.w2mesh
        # (the gloves template uses a model with tucked-in sleeves; a real mod brings its own for that one)
        dlc\bob\data\characters\models\geralt\armor\armor__wolf\t_05a_mg__wolf.w2mesh: mymod\look\look_armor.w2mesh
      groups:                              # texture group (default by format: CharacterDiffuse; Geralt's armors
        mymod\look\look_armor_d01.dds: WorldDiffuse   # use both, this is the one the game gives this texture)
    $text:
      name: {en: ItemXL Example Look, de: ItemXL-Beispiel (eigenes Aussehen)}
    $sell: [_store__Rich_District_Armorer]

  # the other way: the mod's own templates (.w2ent, from WolvenKit/REDkit; here made from the game's with
  # "itemxl-cli retarget"), and an own inventory icon
  ItemXL Example Own:
    $base: Gryphon Armor 4
    $appearance:
      from: mymod\own\own_armor.w2ent
      gloves: mymod\own\own_armor.w2ent
      icon: mymod\icons\own_armor.dds
    $text:
      name: {en: ItemXL Example Own Template, de: ItemXL-Beispiel (eigene Vorlage)}
    $sell: [_store__Rich_District_Armorer]
"""


def mesh_with_texture(data: bytes, old: str, new: str) -> bytes:
    """The model with its diffuse texture import changed. Its geometry is an external file (buffer row offset 0),
    kept as it is (the SDK writer would make it inline)."""
    c = CR2W(data)
    off, cnt, _ = c.tables[5]
    rows = [data[off + 24 * i: off + 24 * (i + 1)] for i in range(cnt)]
    hit = [imp for imp in c.imports if imp[0].lower() == old.lower()]
    hit[0][0] = new
    for b in c.buffers:
        b[2] = b""
    out = bytearray(c.save())
    boff = struct.unpack_from("<I", out, 0x28 + 5 * 12)[0]
    for i, r in enumerate(rows):
        out[boff + 24 * i: boff + 24 * (i + 1)] = r
    struct.pack_into("<I", out, 0x28 + 5 * 12 + 8, zlib.crc32(b"".join(rows)))
    size = struct.unpack_from("<I", out, 24)[0]
    struct.pack_into("<I", out, 28, size)
    out = out[:size]
    struct.pack_into("<I", out, 32, 0xDEADBEEF)
    struct.pack_into("<I", out, 32, zlib.crc32(bytes(out[:0xA0])))
    return bytes(out)


def dds_dxt1(w: int, h: int, mips: int, pixels: bytes) -> bytes:
    # "DDS ", header (size, flags, height, width, linear size, depth, mips, 11 reserved), pixel format (size,
    # flags = FOURCC, "DXT1", bit count, 4 masks), caps (texture, mipmap, complex), caps2-4, reserved
    head = struct.pack("<4sIIIIIII44sII4sIIIIIIIIII", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000,
                       h, w, max(1, w // 4) * max(1, h // 4) * 8, 0, mips, b"\0" * 44,
                       32, 0x4, b"DXT1", 0, 0, 0, 0, 0, 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    assert len(head) == 128
    return head + pixels


def swap_red_blue_dxt1(pix: bytes) -> bytes:
    out = bytearray(pix)
    for i in range(0, len(out) - 7, 8):
        for k in (0, 2):
            c = struct.unpack_from("<H", out, i + k)[0]
            r, g, b = c >> 11, (c >> 5) & 63, c & 31
            struct.pack_into("<H", out, i + k, (b << 11) | (g << 5) | r)
    return bytes(out)


def swap_red_blue_dxt5(pix: bytes) -> bytes:
    """DXT5: 8 bytes alpha, then a DXT1 colour block."""
    out = bytearray(pix)
    for i in range(0, len(out) - 15, 16):
        out[i + 8:i + 16] = swap_red_blue_dxt1(bytes(out[i + 8:i + 16]))
    return bytes(out)


def dds_dxt5(w: int, h: int, pixels: bytes) -> bytes:
    d = bytearray(dds_dxt1(w, h, 1, pixels))
    d[84:88] = b"DXT5"
    struct.pack_into("<I", d, 20, w * h)   # linear size: 16 bytes per 4x4 block
    return bytes(d)


def texture_pixels(path: str, cache_file: str = ""):
    """A texture of the game as (width, height, mips, format, key, pixels of all mips)."""
    cache = texcache.Cache.open(cache_file or os.path.join(paths.GAME, "content", "content0", "texture.cache"))
    e = cache.find(path)
    pix = b"".join(zlib.decompress(z) for _ps, _s, _l, z in cache.pieces(e))
    return e[6], e[7], e[8], e[13], e[0], pix


def make(mod_dir: str) -> str:
    d = Depot(paths.GAME, mods=False)
    base = os.path.join(mod_dir, "Mods", MOD, "itemxl")
    files = os.path.join(base, "files")
    os.makedirs(os.path.join(files, "mymod", "look"), exist_ok=True)
    with open(os.path.join(files, MESH), "wb") as f:
        f.write(mesh_with_texture(d.read(SRC_MESH), SRC_TEX, TEX))
    with open(os.path.join(files, MESH + ".1.buffer"), "wb") as f:
        f.write(d.read(SRC_MESH + ".1.buffer"))
    w, h, mips, fmt, _key, pix = texture_pixels(SRC_TEX)
    assert fmt == 7, fmt
    with open(os.path.join(files, TEX[:-4] + ".dds"), "wb") as f:
        f.write(dds_dxt1(w, h, mips, swap_red_blue_dxt1(pix)))
    # own templates: the game's Wolf pair, pointed at our model / our meshes template
    own = os.path.join(files, "mymod", "own")
    os.makedirs(own, exist_ok=True)
    tmp = os.path.join(own, "_in.w2ent")
    for src, dst, old, new in ((WOLF_MESHES, OWN_MESHES, SRC_MESH, MESH), (WOLF, OWN, WOLF_MESHES, OWN_MESHES)):
        open(tmp, "wb").write(d.read(src))
        r = subprocess.run([paths.CLI, "retarget", tmp, os.path.join(files, dst), f"{old}={new}"],
                           capture_output=True, text=True)
        assert r.returncode == 0 and not r.stdout.startswith("0 "), r.stdout + r.stderr
    os.remove(tmp)
    # the icon: the game's Wolf armor icon, red and blue swapped
    os.makedirs(os.path.join(files, "mymod", "icons"), exist_ok=True)
    w, h, mips, fmt, _key, pix = texture_pixels(ICON, ICON_CACHE)
    assert fmt == 8 and mips == 1, (fmt, mips)
    with open(os.path.join(files, "mymod", "icons", "own_armor.dds"), "wb") as f:
        f.write(dds_dxt5(w, h, swap_red_blue_dxt5(pix)))
    with open(os.path.join(base, "look.yaml"), "w", encoding="utf-8") as f:
        f.write(YAML)
    return os.path.join(mod_dir, "Mods", MOD)


if __name__ == "__main__":
    print("written:", make(sys.argv[1]))
