"""$appearance and the resource formats it writes.

1. CR2W: game resources read and written again by itemxl-cli are the same bytes (a template, a "_meshes" template,
   a model with its geometry in an external .1.buffer, a texture).
2. Textures: the Wolf armor's texture as a DDS -> itemxl-cli makes the same .xbm the game has (same cache key given),
   and a texture.cache whose entry has the game's size, format, mips and pixels.
3. Build: the example mod (tools\\make_look_example.py) -> templates copied and pointed at the own model, the
   .reddlc with the templates mounter, the geometry in buffers0.bundle (listed in metadata.store), the texture in
   texture.cache, the item's equip_template and its gloves variant.
"""
import os
import shutil
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "third_party", "TW3SE-SDK", "tools"))
from cr2w import Depot, CR2W  # noqa: E402
import texcache  # noqa: E402
sys.path.insert(0, HERE)
import w3files  # noqa: E402
from tools import paths, make_look_example as look  # noqa: E402

GRYPHON = "dlc\\bob\\data\\items\\bodyparts\\geralt_items\\trunk\\witcher_gryphon\\t_01_mg__gryphon_lvl5.w2ent"
WOLF_MESHES = "dlc\\bob\\data\\characters\\models\\geralt\\armor\\armor__wolf\\t_01_mg__wolf_lvl5_meshes.w2ent"


def cli(*args):
    return subprocess.run([paths.CLI, *args], capture_output=True, text=True, encoding="utf-8", errors="replace")


def formats(work: str, d: Depot) -> list:
    problems = []
    for p in (GRYPHON, WOLF_MESHES, look.SRC_MESH, look.SRC_TEX):
        src, out = os.path.join(work, "in.bin"), os.path.join(work, "out.bin")
        data = d.read(p)
        open(src, "wb").write(data)
        r = cli("cr2w", src, out)
        if r.returncode or open(out, "rb").read() != data:
            problems.append(f"cr2w: {p} is not written back the same {r.stderr}")
    w, h, mips, fmt, key, pix = look.texture_pixels(look.SRC_TEX)
    dds = os.path.join(work, "t.dds")
    open(dds, "wb").write(look.dds_dxt1(w, h, mips, pix))
    xbm, tc = os.path.join(work, "t.xbm"), os.path.join(work, "t.cache")
    r = cli("xbm", dds, look.SRC_TEX, str(key), xbm, tc, "WorldDiffuse")   # (the group the game gives it)
    if r.returncode:
        return problems + ["xbm: " + r.stderr]
    if open(xbm, "rb").read() != d.read(look.SRC_TEX):
        problems.append("xbm: not the game's .xbm for the game's texture")
    c = texcache.Cache(open(tc, "rb").read())
    e = c.find(look.SRC_TEX)
    if not e or (e[0], e[6], e[7], e[8], e[13]) != (key, w, h, mips, fmt):
        problems.append(f"texture.cache: entry {e and e[:14]}")
    elif b"".join(zlib.decompress(z) for _a, _b, _c, z in c.pieces(e)) != pix:
        problems.append("texture.cache: other pixels")
    # Corrupted CR2W header (table offset out of bounds) must fail cleanly instead of reading out of bounds
    bad_cr2w = bytearray(d.read(GRYPHON))
    struct.pack_into("<II", bad_cr2w, 0x28, 0x7FFFFFFF, 100)
    open(src, "wb").write(bad_cr2w)
    r_bad = cli("cr2w", src, out)
    if r_bad.returncode == 0 or "outside the file" not in r_bad.stderr:
        problems.append(f"cr2w bounds: expected clean error on out-of-bounds table, got rc={r_bad.returncode} {r_bad.stderr!r}")
    return problems


def metadata_buffers(meta: bytes):
    """The buffer table of a metadata.store and the bundle of each file (see pack.cpp)."""
    p = 16

    def vlq():
        nonlocal p
        b = meta[p]; p += 1
        v, sh, cont = b & 0x3F, 6, b & 0x40
        while cont:
            b = meta[p]; p += 1; v |= (b & 0x7F) << sh; sh += 7; cont = b & 0x80
        return v
    n = vlq(); st = meta[p:p + n]; p += n
    infos = [struct.unpack_from("<8I", meta, p + 32 * i) for i in range(vlq())]; p += 32 * len(infos)
    ents = [struct.unpack_from("<QQII", meta, p + 24 * i) for i in range(vlq())]; p += 24 * len(ents)
    nb = vlq(); p += 24 * nb
    buffers = []
    if meta[p] == 0x80:
        p += 1
    else:
        buffers = [struct.unpack_from("<I", meta, p + 4 + 4 * i)[0] for i in range(vlq())]
    name = lambda o: st[o:st.index(b"\0", o)].decode()   # noqa: E731
    return {name(i[0]): (ents[i[4]][3], i[5], i[6]) for i in infos[1:]}, buffers


def build(work: str) -> list:
    problems = []
    mods = os.path.join(work, "Mods")
    look.make(work)
    out = os.path.join(work, "out")
    r = cli("build", "--game", paths.GAME, "--mods", mods, "--out", out, "--force")
    rep = open(os.path.join(out, "itemxl_report.txt"), encoding="utf-8").read() if os.path.isfile(os.path.join(out, "itemxl_report.txt")) else r.stdout + r.stderr
    if "ERRORS: 0" not in rep:
        problems.append("build errors: " + rep[rep.find("ERRORS"):][:600])
    content = os.path.join(out, "content")
    files = {}
    for b in ("blob0.bundle", "buffers0.bundle"):
        bp = os.path.join(content, b)
        if not os.path.isfile(bp):
            problems.append(f"no {b}")
            continue
        for name, data in w3files.unpack(bp).items():
            files[name] = (b, data)
    name = "itemxl_itemxl_example_look"
    tdir = "dlc\\dlcitemxl\\data\\items\\itemxl\\"
    outer, meshes = tdir + name + ".w2ent", tdir + name + "__t_01_mg__wolf_lvl5_meshes.w2ent"
    for p in (outer, meshes, look.MESH, look.TEX, look.MESH + ".1.buffer", "dlc\\dlcitemxl\\dlcitemxl.reddlc"):
        if p.lower() not in files:
            problems.append(f"not packed: {p}")
    if problems:
        return problems
    if files[(look.MESH + ".1.buffer").lower()][0] != "buffers0.bundle":
        problems.append("the geometry is not in buffers0.bundle")
    imp = lambda p: [i[0].lower() for i in CR2W(files[p.lower()][1]).imports]   # noqa: E731
    if meshes.lower() not in imp(outer):
        problems.append(f"the copied template does not include its copied meshes: {imp(outer)}")
    if look.MESH.lower() not in imp(meshes) or look.SRC_MESH.lower() in imp(meshes):
        problems.append(f"the meshes template does not use the own model: {imp(meshes)}")
    if b"CR4DefinitionsEntitieTemplatesDLCMounter" not in files["dlc\\dlcitemxl\\dlcitemxl.reddlc"][1]:
        problems.append("the .reddlc lacks the templates mounter")
    meta, buffers = metadata_buffers(open(os.path.join(content, "metadata.store"), "rb").read())
    m = meta.get(look.MESH)
    if not m or m[2] != 1 or not buffers:
        problems.append(f"metadata.store: the model's buffer is not listed ({m}, {buffers})")
    c = texcache.Cache(open(os.path.join(content, "texture.cache"), "rb").read())
    e = c.find(look.TEX)
    if not e:
        problems.append("texture.cache lacks the texture")
    elif struct.pack("<I", e[0]) not in files[look.TEX.lower()][1]:
        problems.append("the .xbm does not carry the cache key of its texture.cache entry")
    xml = files["dlc\\dlcitemxl\\data\\gameplay\\items\\itemxl.xml"][1].decode("utf-8-sig")
    i = xml.find('name="ItemXL Example Look"')
    item = xml[i:xml.find("</item>", i)]
    if f'equip_template="{name}"' not in item:
        problems.append("the item does not use the copied template")
    # the gloves variant: named after the variant it replaces ($base: Griffin), copied from "gloves:" (Wolf)
    vname = name + "__t_01a_mg_gryphon_lvl5"
    if f'equip_template="{vname}"' not in item:
        problems.append("the gloves variant does not use its copy: " + item[item.find("<variants"):][:200])
    gl = (tdir + vname + ".w2ent").lower()
    if gl not in files:
        problems.append("no copy for the gloves variant")
    elif not any("wolf" in i for i in imp(gl)):
        problems.append(f"the gloves copy is not made from gloves: t_01a_mg__wolf_lvl5 ({imp(gl)})")
    if b"WorldDiffuse" not in files[look.TEX.lower()][1]:
        problems.append("groups: the .xbm does not have the texture group of the YAML")
    # the own templates (made with itemxl-cli retarget) and the icon: a texture.cache entry like the game's (no .xbm)
    i = xml.find('name="ItemXL Example Own"')
    own = xml[i:xml.find("</item>", i)]
    if 'equip_template="itemxl_itemxl_example_own"' not in own:
        problems.append("own template: the item does not use it")
    if 'icon_path="itemxl/itemxl_example_own.png"' not in own:
        problems.append("icon: icon_path not set: " + own[:300])
    ie = c.find(r"gameplay\gui_new\itemxl\itemxl_example_own.png")   # icon_path itemxl/...
    ge = texcache.Cache.open(os.path.join(paths.GAME, "content", "content0", "texture.cache")).find(look.ICON)
    if not ie:
        problems.append("icon: not in texture.cache")
    elif (ie[6], ie[7], ie[8], ie[11] & 0xFFFF, ie[13]) != (ge[6], ge[7], ge[8], ge[11] & 0xFFFF, ge[13]):
        problems.append(f"icon: entry {ie[:14]} is not like the game's {ge[:14]}")
    def key(path):   # the game's string hash; checked against all of the game's entries below
        h = 0
        for ch in path.lower():
            h = (h * 31 + ord(ch)) & 0xFFFFFFFF
        return h
    gcache = texcache.Cache.open(os.path.join(paths.GAME, "content", "content0", "texture.cache"))
    if any(key(n) != g[0] for n, g in gcache.entries[:2000] if n.endswith((".png", ".xbm"))):
        problems.append("the key rule (h * 31 + c) does not fit the game's texture.cache")
    for n, en in c.entries:
        if en[0] != key(n):
            problems.append(f"texture.cache: the key of {n} is not the hash of its path (the game finds icons by it)")
    if any(k.endswith("own_armor.xbm") for k in files):
        problems.append("icon: made a .xbm too")
    return problems


WOLF = r"dlc\bob\data\items\bodyparts\geralt_items\trunk\witcher_wolf\t_01_mg__wolf_lvl5.w2ent"


def own_template(work: str, d: Depot) -> list:
    """from: a mod's own .w2ent (by depot path): used as it is; a file it uses that is nowhere is an error."""
    problems = []
    for broken in (False, True):
        w = os.path.join(work, "own_broken" if broken else "own")
        files = os.path.join(w, "Mods", "modOwn", "itemxl", "files", "mymod", "own")
        os.makedirs(files)
        data = d.read(WOLF)
        if broken:   # same length: the included _meshes template points nowhere
            data = data.replace(b"t_01_mg__wolf_lvl5_meshes", b"t_01_mg__wolf_lvl5_meshex")
        open(os.path.join(files, "own_armor.w2ent"), "wb").write(data)
        if not broken:   # textures that cost memory for nothing: warnings
            open(os.path.join(files, "opaque.dds"), "wb").write(
                look.dds_dxt5(64, 64, (b"\xff\xff" + b"\0" * 14) * 256))          # DXT5, alpha 255 everywhere, 1 mip
            head = struct.pack("<4sIIIIIII44sIIIIIIIIIIIII", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000, 64, 64, 256, 0, 1,
                               b"\0" * 44, 32, 0x41, 0, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000, 0x1000, 0, 0, 0, 0)
            open(os.path.join(files, "raw.dds"), "wb").write(head + b"\x80" * (64 * 64 * 4))   # uncompressed RGBA
        open(os.path.join(w, "Mods", "modOwn", "itemxl", "own.yaml"), "w").write(
            "item:\n  ItemXL Own:\n    $base: Gryphon Armor 4\n    $appearance:\n"
            "      from: mymod\\own\\own_armor.w2ent\n      gloves: mymod\\own\\own_armor.w2ent\n"
            # a template without dye_* appearances (a "_meshes" one) on a quality 5 item: dyes do nothing
            "  ItemXL Nodye:\n    $base: Gryphon Armor 4\n    quality: 5\n    $appearance:\n"
            "      from: t_01_mg__wolf_lvl5_meshes\n")
        out = os.path.join(w, "out")
        r = cli("build", "--game", paths.GAME, "--mods", os.path.join(w, "Mods"), "--out", out, "--force")
        rp = os.path.join(out, "itemxl_report.txt")
        rep = open(rp, encoding="utf-8").read() if os.path.isfile(rp) else r.stdout + r.stderr
        if broken and "neither in the game" not in rep:
            problems.append("own template: a missing file is not reported")
        if not broken:
            if "ERRORS: 0" not in rep:
                problems.append("own template: " + rep[rep.find("ERRORS"):][:600])
            if "ItemXL Nodye: $appearance: quality 5, but the template has no dye_*" not in rep:
                problems.append("dyes: no warning for a template without dye appearances")
            for part in ("opaque.dds: DXT5 without any transparency", "opaque.dds: no mipmaps",
                         "raw.dds: uncompressed RGBA"):
                if part not in rep:
                    problems.append("dds warnings: missing '" + part + "'")
            if "look_armor_d01.dds: " in rep.split("WARNINGS")[-1]:
                problems.append("dds warnings: a false alarm for the example texture")
            files = w3files.unpack(os.path.join(out, "content", "blob0.bundle"))
            xml = files["dlc\\dlcitemxl\\data\\gameplay\\items\\itemxl.xml"].decode("utf-8-sig")
            if 'equip_template="itemxl_itemxl_own"' not in xml:
                problems.append("own template: the item does not use it")
    return problems


def run():
    work = os.path.join(paths.WORK, "appearance")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    d = Depot(paths.GAME, mods=False)
    return formats(work, d) + build(work) + own_template(work, d)


if __name__ == "__main__":
    p = run()
    print("\n".join(p) or "ok")
    sys.exit(1 if p else 0)
