// $appearance: an item that looks like its own files (ArchiveXL's part).
//
//   item:
//     My Armor:
//       $base: Wolf Armor 4
//       $appearance:
//         from: t_01_mg__wolf_lvl5            # the template to copy (default: the item's equip_template); the game's
//                                             # or the mod's own one in itemxl\files (by name or depot path)
//         gloves: t_01a_mg__wolf_lvl5         # the template for "gloves worn" (default: from, else the item's own)
//         replace:                            # what the copies use -> the mod's own files (depot paths)
//           dlc\bob\data\...\t_05_mg__wolf.w2mesh: mymod\armor\my_armor.w2mesh
//         colors: off                         # drop the template's colour shift (default: keep)
//         icon: mymod\icons\my_armor.dds     # inventory icon (64x128 armor/weapons, 64x64 others; DXT5)
//         groups:                             # texture group of a .dds (default: by format, Character*)
//           mymod\armor\my_armor_d01.dds: WorldDiffuse
//
// Only "icon:" (no from/gloves/replace/colors): just the icon, the item keeps its look.
//
// The mod's own files are in <mod>\itemxl\files\ under their depot paths (a model .w2mesh with its .1.buffer, other
// resources); a .dds there becomes a .xbm of the same path with its pixels in ItemXL's texture.cache.
//
// What is made (all checked in the game):
//   - the template and the templates it includes (the "_meshes" one with the model) are copied to
//     dlc\dlcitemxl\data\items\itemxl\itemxl_<item>.w2ent and ..\itemxl_<item>__<name>.w2ent, their imports changed
//     (also in their embedded copy flatCompiledData); the .reddlc gets a CR4DefinitionsEntitieTemplatesDLCMounter
//     for that folder, else the game does not find them
//   - equip_template of the item = the copy; each <variant equip_template> (the "gloves worn" one $base brings) gets
//     its own copy, else the game shows the old item when gloves are worn
//   - .buffer files go to buffers0.bundle (pack.cpp), else the model stays invisible
#include "itemxl.hpp"

namespace itemxl {

namespace {

std::string slug(const std::string& s) {
    std::string out;
    bool us = false;
    for (unsigned char c : lower(s)) {
        if (std::isalnum(c) && c < 0x80) { out += (char)c; us = false; }
        else if (!us) { out += '_'; us = true; }
    }
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

std::string base_name(const std::string& path) {
    std::string f = path.substr(path.find_last_of('\\') + 1);
    return f.substr(0, f.rfind('.'));
}

}  // namespace

std::string appearance_name(const std::string& item) { return "itemxl_" + slug(item); }

// $appearance out of the item records (applied after all mods, when the item has its final equip_template)
void take_appearances(Value& delta, std::vector<AppearanceReq>& out, const std::string& mod) {
    Value* items = delta.get("item");
    if (!items || !items->is_map()) return;
    for (auto& kv : items->map) {
        Value& spec = kv.second;
        if (!spec.is_map() || !spec.has("$appearance")) continue;
        auto it = std::find_if(spec.map.begin(), spec.map.end(), [](auto& p) { return p.first == "$appearance"; });
        out.push_back({kv.first.substr(0, kv.first.find(" | ")), mod, it->second});
        spec.map.erase(it);
    }
}

void build_appearances(Index& index, const std::vector<AppearanceReq>& reqs, const std::vector<ModFile>& files,
                       const TemplateFinder& find_template,
                       const std::function<bool(const std::string&)>& game_has, AppearanceOut& out, Ledger& led,
                       std::vector<std::string>& warn) {
    const std::string dir = std::string(ITEMXL_DLC_ROOT) + "\\data\\items\\itemxl\\";
    // ---- what the YAMLs say about files: icons (not a model texture) and texture groups
    static const std::set<std::string> GROUPS = {
        "CharacterDiffuse", "CharacterDiffuseWithAlpha", "CharacterNormal", "CharacterNormalmapGloss",
        "WorldDiffuse", "WorldDiffuseWithAlpha", "WorldNormal", "NormalmapGloss", "QualityColor", "QualityR",
        "Default"};
    std::set<std::string> icons;
    std::map<std::string, std::string> groups;   // lower .dds path -> group
    for (auto& r : reqs) {
        if (!r.spec.is_map()) continue;
        if (const Value* iv = r.spec.get("icon")) icons.insert(lower(iv->text()));
        if (const Value* gv = r.spec.get("groups"); gv && gv->is_map())
            for (auto& kv : gv->map) {
                if (!GROUPS.count(kv.second.text()))
                    led.errors.push_back(r.mod + ": item/" + r.item + ": $appearance: groups: unknown texture group '" +
                                         kv.second.text() + "'");
                else
                    groups[lower(kv.first)] = kv.second.text();
            }
    }
    std::map<std::string, const ModFile*> by_path;
    for (auto& mf : files) by_path[lower(mf.path)] = &mf;
    // ---- the mods' own files; a .dds becomes .xbm + texture.cache entry
    for (auto& mf : files) {
        Bytes data;
        if (!read_file(mf.disk, data)) { warn.push_back(mf.mod + ": cannot read " + mf.disk); continue; }
        if (icons.count(lower(mf.path))) continue;   // made below, per item
        if (ends_with(lower(mf.path), ".dds")) {
            std::string xbm = mf.path.substr(0, mf.path.size() - 4) + ".xbm";
            TextureInfo info;
            Bytes pixels;
            std::string err;
            if (!read_dds(data, mf.path, info, pixels, err)) { led.errors.push_back(mf.mod + ": " + mf.path + ": " + err); continue; }
            if (auto g = groups.find(lower(mf.path)); g != groups.end()) info.group = g->second;
            // what costs graphics memory for nothing (warnings, the texture is made all the same)
            const std::string who = mf.mod + ": " + mf.path + ": ";
            if (info.format == 253)
                warn.push_back(who + "uncompressed RGBA (8 times the memory of DXT1); save it as DXT1, DXT5 or BC7");
            if (info.mips.size() == 1 && (info.width > 32 || info.height > 32))
                warn.push_back(who + "no mipmaps: it flickers at a distance and cannot be streamed; save it with mipmaps");
            if (info.format == 8 && info.compression == "TCM_DXTAlpha") {   // (not normal maps: their alpha is gloss)
                bool alpha = false;                                          // DXT5 block: alpha0, alpha1, 6 index bytes
                for (size_t i = 0; i + 16 <= info.sizes[0] && !alpha; i += 16)
                    alpha = !(pixels[i] == 255 && (pixels[i + 1] == 255 ||
                              std::all_of(pixels.begin() + i + 2, pixels.begin() + i + 8, [](uint8_t b) { return b == 0; })));
                if (!alpha)
                    warn.push_back(who + "DXT5 without any transparency; DXT1 is half the size");
            }
            uint32_t key = texture_key(xbm);
            auto pieces = texture_pieces(info, pixels);
            out.files[xbm] = make_xbm(info, key, pieces.back());
            out.textures.push_back({xbm, key, info, std::move(pieces)});
        } else {
            out.files[mf.path] = std::move(data);
        }
    }

    // ---- copies of templates, by source name and the replacements they got
    std::map<std::string, std::string> copied;   // "<new name>" -> done
    std::set<std::string> no_dye;                 // items whose copied template has no dye appearances
    auto copy_template = [&](const std::string& mod, const std::string& item, const std::string& src_name,
                             const std::string& new_name, const std::map<std::string, std::string>& replace,
                             bool colors_off) -> bool {
        Bytes outer;
        std::string src_path;
        // a depot path ("mymod\armor\my_armor.w2ent") or a name: templates are found by name
        bool is_path = src_name.find('\\') != std::string::npos || ends_with(lower(src_name), ".w2ent");
        if (!find_template(is_path ? base_name(src_name) : src_name, outer, src_path)) {
            led.errors.push_back(mod + ": item/" + item + ": $appearance: template '" + src_name + "' not found");
            return false;
        }
        std::string err;
        Cr2w f;
        if (!f.read(outer, err)) { led.errors.push_back(mod + ": " + src_path + ": " + err); return false; }
        std::map<std::string, std::string> paths = replace;
        std::set<std::string> used;
        int changed_total = 0;
        // the templates it includes (the "_meshes" one): copied too, with the replacements
        for (auto& imp : f.imports) {
            if (!ends_with(lower(imp.path), ".w2ent")) continue;
            Bytes inner;
            std::string inner_path;
            if (!find_template(base_name(imp.path), inner, inner_path)) {
                warn.push_back(mod + ": item/" + item + ": $appearance: included template " + imp.path + " not found");
                continue;
            }
            int changed = 0;
            Bytes re = cr2w_retarget(inner, replace, changed, err);
            if (re.empty()) { led.errors.push_back(mod + ": " + imp.path + ": " + err); return false; }
            changed_total += changed;
            std::string to = dir + new_name + "__" + base_name(imp.path) + ".w2ent";
            out.files[to] = re;
            paths[lower(imp.path)] = to;
        }
        int changed = 0;
        Bytes re = cr2w_retarget(outer, paths, changed, err);
        if (re.empty()) { led.errors.push_back(mod + ": " + src_path + ": " + err); return false; }
        changed_total += changed;
        if (colors_off) {
            Cr2w g;
            if (g.read(re, err)) {
                for (auto& e : g.exports) g.remove_prop(e, "coloringEntries");
                re = g.write();
            }
        }
        out.files[dir + new_name + ".w2ent"] = re;
        out.templates = true;
        // dyes: the game colours an item with quality 5 through the template's dye_* appearances
        const std::string dye = "dye_default";
        if (std::search(re.begin(), re.end(), dye.begin(), dye.end()) == re.end())
            no_dye.insert(item);
        if (!replace.empty() && changed_total == 0)
            warn.push_back(mod + ": item/" + item + ": $appearance: no path of 'replace' is used by " + src_name +
                           " or the templates it includes");
        return true;
    };

    for (auto& r : reqs) {
        auto it = index.rec.find({"item", r.item});
        std::vector<Rec> hits = it == index.rec.end() ? std::vector<Rec>{} : targets(it->second);
        if (hits.empty()) { led.errors.push_back(r.mod + ": item/" + r.item + ": $appearance: no such item"); continue; }
        // the icon: a texture.cache entry "<dlc>\\data\\gameplay\\gui_new\\icons\\itemxl\\<item>.png" (no .xbm, like the
        // game's icons); icon_path "icons/..." is looked up under gameplay\\gui_new of the game and each DLC
        if (const Value* iv = r.spec.is_map() ? r.spec.get("icon") : nullptr) {
            auto f = by_path.find(lower(iv->text()));
            Bytes data;
            TextureInfo info;
            Bytes pixels;
            std::string err;
            if (f == by_path.end())
                led.errors.push_back(r.mod + ": item/" + r.item + ": $appearance: icon " + iv->text() +
                                     " is not in a mod's itemxl\\files");
            else if (!read_file(f->second->disk, data) || !read_dds(data, f->second->path, info, pixels, err))
                led.errors.push_back(r.mod + ": " + iv->text() + ": " + (err.empty() ? "cannot read" : err));
            else {
                // as mods that bring icons do: icon_path "x/y.png" -> gameplay\gui_new\x\y.png
                // ("icons/..." under a DLC path or under gameplay\gui_new\icons did not show, game test 10.11.)
                std::string png = "itemxl/" + slug(r.item) + ".png";
                std::string name = "gameplay\\gui_new\\itemxl\\" + slug(r.item) + ".png";
                // the inventory scales an icon to the item's cells (game test 12.11.: 32x64 and 128x256 both fill
                // them), so only the shape matters: twice as high as wide for an item two cells high (grid_size 2,
                // weapons and armor), square for the others
                const std::string* gs = hits[0].el->attr("grid_size");
                bool tall = gs && *gs == "2";
                if (info.height != (tall ? 2 * info.width : info.width))
                    warn.push_back(r.mod + ": item/" + r.item + ": $appearance: icon is " + std::to_string(info.width) +
                                   "x" + std::to_string(info.height) + ", but the item is " +
                                   (tall ? "two cells high (grid_size 2): use 1:2, like 64x128"
                                         : "one cell (no grid_size 2): use a square, like 64x64"));
                info.mips.resize(1);                                  // icons have one mip
                info.sizes.resize(1);
                info.resident_mip = 0;
                pixels.resize(info.sizes[0]);
                uint32_t key = texture_key(name);
                out.textures.push_back({name, key, info, texture_pieces(info, pixels)});
                for (auto& h : hits) { h.el->set_attr("icon_path", png); h.file->dirty = true; }
            }
        }
        bool look = r.spec.is_map() && (r.spec.has("from") || r.spec.has("gloves") || r.spec.has("replace") ||
                                        r.spec.has("colors"));
        if (!look && r.spec.is_map() && r.spec.has("icon")) continue;   // only the icon
        const Value* fromv = r.spec.is_map() ? r.spec.get("from") : nullptr;
        const std::string* et = hits[0].el->attr("equip_template");
        std::string from = fromv && !fromv->is_null() ? fromv->text() : (et ? *et : "");
        if (from.empty()) { led.errors.push_back(r.mod + ": item/" + r.item + ": $appearance: no 'from' and no equip_template"); continue; }
        std::map<std::string, std::string> replace;
        if (const Value* rp = r.spec.is_map() ? r.spec.get("replace") : nullptr; rp && rp->is_map())
            for (auto& kv : rp->map) replace[lower(kv.first)] = kv.second.text();
        const Value* cv = r.spec.is_map() ? r.spec.get("colors") : nullptr;
        bool colors_off = cv && (cv->text() == "off" || cv->text() == "False");
        std::string name = appearance_name(r.item);
        if (!copy_template(r.mod, r.item, from, name, replace, colors_off)) continue;
        for (auto& h : hits) {
            h.el->set_attr("equip_template", name);
            if (Node* vs = h.el->find("variants"))
                for (auto& v : vs->children) {
                    const std::string* ve = v->attr("equip_template");
                    if (!ve || ve->empty() || starts_with(*ve, "itemxl_")) continue;
                    std::string vname = name + "__" + slug(*ve);
                    // the source of a variant: "gloves:" for the gloves one, else "from:" (the item's own look),
                    // else the variant's template (with the replacements)
                    const std::string* cat = v->attr("category");
                    const Value* gv = r.spec.is_map() ? r.spec.get("gloves") : nullptr;
                    std::string src = gv && cat && *cat == "gloves" ? gv->text() : fromv && !fromv->is_null() ? from : *ve;
                    if (!copied.count(vname) && copy_template(r.mod, r.item, src, vname, replace, colors_off))
                        copied[vname] = src;
                    if (copied.count(vname)) v->set_attr("equip_template", vname);
                }
            h.file->dirty = true;
        }
        if (no_dye.count(r.item)) {
            const std::string* q = hits[0].el->attr("quality");
            bool nodye_tag = false;
            if (Node* t = hits[0].el->find("tags")) nodye_tag = t->text.find("noDye") != std::string::npos;
            if (q && *q == "5" && !nodye_tag)
                warn.push_back(r.mod + ": item/" + r.item + ": $appearance: quality 5, but the template has no dye_* "
                               "appearances: dyes do nothing on it (tag noDye to say so)");
        }
    }

    // ---- every file the copies use must be there: in the game or among the mods' files (else: invisible item)
    std::set<std::string> have;
    for (auto& kv : out.files) have.insert(lower(kv.first));
    for (auto& kv : out.files) {
        if (!starts_with(lower(kv.first), lower(dir)) || !ends_with(lower(kv.first), ".w2ent")) continue;
        Cr2w f;
        std::string err;
        if (!f.read(kv.second, err)) continue;
        for (auto& imp : f.imports)
            if (!imp.path.empty() && !have.count(lower(imp.path)) && !game_has(imp.path))
                led.errors.push_back(kv.first + ": uses " + imp.path + ", which is neither in the game nor in a mod's itemxl\\files");
    }
}

}  // namespace itemxl
