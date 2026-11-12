// The whole ItemXL build, as the game sees its files.
//
// Inputs
//   base      <game>\content\*\bundles\*.bundle and <game>\dlc\*\content\**.bundle: every definition XML
//             (gameplay\... and dlc\<name>\data\gameplay\...). DLC folders of mods in <game>\dlc are
//             patched like the game's own.
//   mods      <Mods>\<mod>\content\*.bundle: a definition XML that replaces a vanilla one is turned into a delta
//             <Mods>\<mod>\itemxl\**\*.yaml: deltas of that mod
//             order: mods.settings (Enabled=0 is skipped; the lowest Priority wins, so it is applied last);
//             mods missing there come after the listed ones, by name, like the game does without mods.settings
//   global    <game>\bin\itemxl\**\*.yaml (like TweakXL's r6\tweaks), applied after all mods, by file name
// Output
//   <out_mod>\content\blob0.bundle + metadata.store   the merged XMLs
//   <out_mod>\tw3se\strings\<lang>.txt                 texts from $text (TW3SE reads them)
//   <out_mod>\itemxl_report.txt                         what happened
//   <out_mod>\itemxl.stamp                              the state of the inputs; same stamp -> nothing to do
#include <windows.h>

#include "itemxl.hpp"
#include "version.hpp"

#include <algorithm>
#include <chrono>
#include <regex>
#include <set>
#include <unordered_set>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace itemxl {

static const char* OUTPUT_MOD = "mod00000_ItemXL";
static const char* VERSION = ITEMXL_VERSION;

namespace {

std::string u8(const fs::path& p) { return p.u8string(); }

std::vector<std::string> list_dirs(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::u8path(dir), ec))
        if (e.is_directory(ec)) out.push_back(u8(e.path().filename()));
    std::sort(out.begin(), out.end());
    return out;
}

// Files below dir whose names end in one of exts (lower case), recursively, sorted by full path.
std::vector<std::string> find_files(const std::string& dir, std::vector<std::string> exts, bool recursive = true) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(dir), ec)) return out;
    auto take = [&](const fs::directory_entry& e) {
        if (!e.is_regular_file(ec)) return;
        std::string n = lower(u8(e.path().filename()));
        for (auto& x : exts) if (ends_with(n, x)) { out.push_back(u8(e.path())); return; }
    };
    if (recursive) for (auto& e : fs::recursive_directory_iterator(fs::u8path(dir), ec)) take(e);
    else for (auto& e : fs::directory_iterator(fs::u8path(dir), ec)) take(e);
    std::sort(out.begin(), out.end());
    return out;
}

// The files of a mod folder the build looks at, found once: the stamp and the build use the same lists. Size and
// time come from the directory listing itself (no further call per file).
struct Found { std::string path; uintmax_t size = 0; long long time = 0; };
// size and time of the DLL or EXE this code runs in: a new build of ItemXL changes it
static std::string own_binary_stamp() {
    HMODULE h = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&own_binary_stamp), &h))
        return "";
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(h, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return "";
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &a)) return "";
    return std::to_string(((uint64_t)a.nFileSizeHigh << 32) | a.nFileSizeLow) + " " +
           std::to_string(((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime);
}

std::vector<Found> scan_files(const std::string& dir, const char* ext, bool recursive) {
    std::vector<Found> out;
    std::error_code ec;
    auto take = [&](const fs::directory_entry& e) {
        if (!e.is_regular_file(ec)) return;
        if (*ext && !ends_with(lower(u8(e.path().filename())), ext)) return;
        Found f;
        f.path = u8(e.path());
        f.size = e.file_size(ec);
        f.time = (long long)e.last_write_time(ec).time_since_epoch().count();
        out.push_back(std::move(f));
    };
    if (recursive) {
        for (auto it = fs::recursive_directory_iterator(fs::u8path(dir), ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) take(*it);
    } else {
        for (auto it = fs::directory_iterator(fs::u8path(dir), ec);
             !ec && it != fs::directory_iterator(); it.increment(ec)) take(*it);
    }
    std::sort(out.begin(), out.end(), [](const Found& a, const Found& b) { return a.path < b.path; });
    return out;
}
struct ModScan { std::vector<Found> bundles, xl; };   // the bundles in content, everything below itemxl

std::string text_of(const Bytes& b) { return std::string(b.begin(), b.end()); }

struct ModSettings {
    struct Entry { bool enabled = true; long priority = 0; };
    std::map<std::string, Entry> by_name;   // lower-case folder name
};

ModSettings read_mods_settings(const std::string& path) {
    ModSettings ms;
    Bytes b;
    if (path.empty() || !read_file(path, b)) return ms;
    std::istringstream in(text_of(b));
    std::string line, section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (starts_with(line, "\xEF\xBB\xBF")) line = line.substr(3);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[' && line.back() == ']') { section = lower(line.substr(1, line.size() - 2)); ms.by_name[section]; continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos || section.empty()) continue;
        std::string k = lower(trim(line.substr(0, eq))), v = trim(line.substr(eq + 1));
        if (k == "enabled") ms.by_name[section].enabled = v != "0";
        if (k == "priority") ms.by_name[section].priority = std::strtol(v.c_str(), nullptr, 10);
    }
    return ms;
}

// Mod folders in the order they apply: the winner last.
std::vector<std::string> mod_order(const std::string& mods_dir, const ModSettings& ms) {
    std::vector<std::string> listed, rest;
    for (auto& d : list_dirs(mods_dir)) {
        std::string l = lower(d);
        if (l == lower(OUTPUT_MOD) || l == lower(OUTPUT_MOD) + ".tmp" || l == lower(OUTPUT_MOD) + ".old" ||
            l == "mod0000_mergedfiles")
            continue;
        auto it = ms.by_name.find(l);
        if (it != ms.by_name.end()) { if (it->second.enabled) listed.push_back(d); }
        else rest.push_back(d);
    }
    std::stable_sort(listed.begin(), listed.end(), [&](auto& a, auto& b) {
        return ms.by_name.at(lower(a)).priority < ms.by_name.at(lower(b)).priority;
    });
    std::sort(rest.begin(), rest.end(), [](auto& a, auto& b) { return lower(a) < lower(b); });
    std::vector<std::string> win_first = listed;
    win_first.insert(win_first.end(), rest.begin(), rest.end());
    std::reverse(win_first.begin(), win_first.end());
    return win_first;
}

void stamp_add(std::string& st, const std::string& path) {
    std::error_code ec;
    auto p = fs::u8path(path);
    auto size = fs::file_size(p, ec);
    auto t = fs::last_write_time(p, ec).time_since_epoch().count();
    st += path + "|" + std::to_string(size) + "|" + std::to_string((long long)t) + "\n";
}

std::string now_text() {
    std::time_t t = std::time(nullptr);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}

}  // namespace

std::string serialize_def(const DefFile& f) {
    auto root = f.root->clone();
    xml_indent(*root, "\t");
    std::string text = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" + xml_to_string(*root) + "\n";
    std::string crlf = "\xEF\xBB\xBF";
    for (char c : text) {
        if (c == '\n') crlf += "\r\n";
        else crlf += c;
    }
    return crlf;
}

BuildResult build(const BuildOptions& opt) {
    BuildResult res;
    auto log = [&](const std::string& m) { if (opt.log) opt.log(m); };
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::string> warn, report;
    // Notes about the game's own files (base game and CD Projekt's DLCs): no mod causes them, so they get a section
    // of their own instead of standing between the warnings a mod author has to look at.
    std::vector<std::string> game_notes;
    auto official = [](const std::string& path) {
        std::string l = lower(path);
        if (starts_with(l, "gameplay\\")) return true;
        static const std::regex dlc(R"(^dlc\\(ep1|bob|dlc\d+)\\)");
        return std::regex_search(l, dlc);
    };
    auto note_file = [&](const std::string& path, const std::string& text) {
        (official(path) ? game_notes : warn).push_back(text);
    };
    // where the time goes: "stamp 0.1 vanilla 0.6 ..." in the log
    std::string laps;
    auto lap_start = t0;
    auto lap = [&](const char* what) {
        auto now = std::chrono::steady_clock::now();
        char b[48];
        std::snprintf(b, sizeof b, "%s %.2f  ", what, std::chrono::duration<double>(now - lap_start).count());
        laps += b;
        lap_start = now;
    };

    // ---- inputs --------------------------------------------------------
    std::vector<std::string> vanilla_bundles;
    for (auto& d : list_dirs(opt.game + "\\content"))
        for (auto& b : find_files(opt.game + "\\content\\" + d, {".bundle"})) vanilla_bundles.push_back(b);
    size_t first_dlc_bundle = vanilla_bundles.size();       // from here on: bundles of DLC folders
    std::string dlc = opt.dlc_dir.empty() ? opt.game + "\\dlc" : opt.dlc_dir;
    for (auto& d : list_dirs(dlc))
        for (auto& b : find_files(dlc + "\\" + d + "\\content", {".bundle"})) vanilla_bundles.push_back(b);

    lap("game-list");
    ModSettings ms = read_mods_settings(opt.mods_settings);
    auto mods = mod_order(opt.mods_dir, ms);
    auto global_yaml = find_files(opt.game + "\\bin\\itemxl", {".yaml", ".yml"});
    lap("mod-list");

    std::string stamp = std::string("ItemXL ") + VERSION + "\n";
    // a new build of ItemXL itself (a fix in how files are read, a change to its script) rebuilds once, even without
    // a new version number
    stamp += "built " + own_binary_stamp() + "\n";
    stamp += "script " + std::to_string(fnv1a64(itemxl_script("", 0, 0))) + "\n";
    if (!opt.environment.empty()) stamp += opt.environment + "\n";
    Bytes msb;
    if (read_file(opt.mods_settings, msb)) stamp += "mods.settings " + std::to_string(fnv1a64(text_of(msb))) + "\n";
    for (auto& b : vanilla_bundles) stamp_add(stamp, b);
    lap("game-stat");
    std::map<std::string, ModScan> scans;                // each mod folder is listed once
    double t_content = 0, t_xl = 0;
    for (auto& m : mods) {
        stamp += "mod " + m + "\n";
        ModScan& sc = scans[m];
        auto a = std::chrono::steady_clock::now();
        // the bundles, and the mod's own texts (<lang>.w3strings): `!append` to a text reads them
        for (auto& f : scan_files(opt.mods_dir + "\\" + m + "\\content", "", false)) {
            std::string l = lower(f.path);
            if (ends_with(l, ".bundle")) sc.bundles.push_back(f);
            else if (ends_with(l, ".w3strings"))
                stamp += f.path + "|" + std::to_string(f.size) + "|" + std::to_string(f.time) + "\n";
        }
        auto b2 = std::chrono::steady_clock::now();
        sc.xl = scan_files(opt.mods_dir + "\\" + m + "\\itemxl", "", true);   // the YAMLs and the own files
        t_content += std::chrono::duration<double>(b2 - a).count();
        t_xl += std::chrono::duration<double>(std::chrono::steady_clock::now() - b2).count();
        for (auto* list : {&sc.bundles, &sc.xl})
            for (auto& f : *list)
                stamp += f.path + "|" + std::to_string(f.size) + "|" + std::to_string(f.time) + "\n";
    }
    for (auto& y : global_yaml) stamp_add(stamp, y);
    // Script Merger's output is no input (ItemXL merges the original mods itself), but a definition file merged
    // there can win over ItemXL's result: its bundles are looked at for the warning below
    std::vector<Found> merged_bundles;
    for (auto& d : list_dirs(opt.mods_dir))
        if (lower(d) == "mod0000_mergedfiles")
            for (auto& f : scan_files(opt.mods_dir + "\\" + d + "\\content", ".bundle", false)) {
                merged_bundles.push_back(f);
                stamp += f.path + "|" + std::to_string(f.size) + "|" + std::to_string(f.time) + "\n";
            }
    {
        char b[96];
        std::snprintf(b, sizeof b, "%zu mods: content %.2f itemxl %.2f  ", mods.size(), t_content, t_xl);
        laps += b;
    }
    std::string stamp_path = opt.out_mod + "\\itemxl.stamp";
    Bytes old_stamp;
    lap("stamp");
    if (!opt.force && read_file(stamp_path, old_stamp) && text_of(old_stamp) == stamp) {
        log("nothing changed, keeping " + opt.out_mod + " (" + laps + ")");
        res.built = false;
        return res;
    }

    // ---- vanilla -------------------------------------------------------
    // lower path -> file; a later bundle wins, the load order counts from the first time a path is seen
    struct RawFile { std::string path; Bytes data; bool from_dlc_folder = false; int order = 0; };
    std::set<std::string> mod_dlc_files;                       // definition files of DLCs inside mods' bundles
    std::map<std::string, RawFile> raw;
    std::vector<std::vector<BundleEntry>> vanilla_entries;   // kept: $appearance looks through them again
    for (size_t bi = 0; bi < vanilla_bundles.size(); bi++) {
        const std::string& b = vanilla_bundles[bi];
        vanilla_entries.push_back(bundle_entries(b));
        for (auto& e : vanilla_entries.back()) {
            if (!is_definition_xml(e.name)) continue;
            Bytes data;
            if (!bundle_read(e, data)) { note_file(e.name, e.name + ": cannot read from " + b); continue; }
            if (!is_definition_data(e.name, data)) continue;
            std::string key = lower(e.name);
            auto it = raw.find(key);
            int order = it == raw.end() ? (int)raw.size() : it->second.order;
            raw[key] = RawFile{e.name, std::move(data), bi >= first_dlc_bundle, order};
        }
    }
    // A mod may bring its own DLC inside its bundle in Mods\ (the game finds a .reddlc in any bundle): its new
    // definition files are copies like a DLC folder's, in the mods' load order. A file the game already has is an
    // override of that file instead; the import below turns those into changes.
    for (auto& m : mods) {
        for (auto& bf : scans[m].bundles) {
            for (auto& e : bundle_entries(bf.path)) {
                if (!is_definition_xml(e.name) || !starts_with(lower(e.name), "dlc\\")) continue;
                std::string key = lower(e.name);
                if (raw.count(key)) continue;
                Bytes data;
                if (!bundle_read(e, data)) { warn.push_back(m + ": " + e.name + ": cannot read"); continue; }
                if (!is_definition_data(e.name, data)) continue;
                raw[key] = RawFile{e.name, std::move(data), true, (int)raw.size()};
                mod_dlc_files.insert(key);
            }
        }
    }

    lap("read");
    auto parse_set = [&](std::vector<std::unique_ptr<DefFile>>& store, std::map<std::string, DefFile*>& files,
                         bool note) {
        for (auto& kv : raw) {
            std::string err;
            int dropped = 0;
            auto root = xml_parse(text_of(kv.second.data), err, &dropped);
            if (!root) {
                if (note) note_file(kv.second.path, kv.second.path + ": not parsed (" + err + ")");
                continue;
            }
            if (dropped && note)
                note_file(kv.second.path, kv.second.path + ": dropped " + std::to_string(dropped) +
                                          " duplicate attribute(s), kept the first of each");
            auto f = std::make_unique<DefFile>();
            f->path = kv.second.path;
            f->root = std::move(root);
            f->rank = kv.second.from_dlc_folder ? 2 : starts_with(kv.first, "gameplay\\") ? 0 : 1;
            f->order = kv.second.order;
            files[kv.first] = f.get();
            store.push_back(std::move(f));
        }
    };
    std::vector<std::unique_ptr<DefFile>> work_store, base_store;
    Index index;
    std::map<std::string, DefFile*> base;
    parse_set(work_store, index.files, true);
    raw.clear();                                              // the unparsed bytes are no longer needed
    lap("parse");
    for (auto& f : work_store) {                             // the untouched copy: what the game has
        auto c = std::make_unique<DefFile>();
        c->path = f->path;
        c->root = f->root->clone();
        c->rank = f->rank;
        c->order = f->order;
        base[lower(f->path)] = c.get();
        base_store.push_back(std::move(c));
    }
    index.build();
    log("vanilla: " + std::to_string(index.files.size()) + " definition files");
    lap("vanilla");

    // ---- mods ------------------------------------------------------------
    Ledger led;
    std::map<std::string, std::map<std::string, std::string>> texts;

    std::set<std::pair<std::string, std::string>> named;          // records the YAML deltas name: checked at the end
    std::map<std::pair<std::string, std::string>, std::string> named_by;

    // The game's word for a diagram per language, from its own text files; read once, only when a $recipe needs it.
    std::map<std::string, std::string> prefixes;
    std::vector<std::string> schematic_keys;
    auto prefix = [&](const std::string& lang) -> std::string {
        auto it = prefixes.find(lang);
        if (it != prefixes.end()) return it->second;
        if (schematic_keys.empty()) {
            // localisation_key_name of the base game's crafting diagrams (not New Game Plus), sorted by item name
            std::map<std::string, std::string> found;
            for (auto& kv : base) {
                DefFile* f = kv.second;
                if (f->rank != 0 || lower(f->path).find("_plus") != std::string::npos) continue;
                records_of(*f, [&](Node*, Node* el) {
                    auto* cat = el->attr("category");
                    auto* key = el->attr("localisation_key_name");
                    auto* nm = el->attr("name");
                    if (el->tag == "item" && cat && *cat == "crafting_schematic" && key && !key->empty() && nm)
                        found.emplace(*nm, *key);
                });
            }
            for (auto& kv : found) schematic_keys.push_back(kv.second);
        }
        W3Strings s;
        std::string p;
        if (s.load(opt.game + "\\content\\content0\\" + lang + ".w3strings")) p = diagram_prefix(s, schematic_keys);
        prefixes[lang] = p;
        return p;
    };

    std::vector<std::pair<std::string, Value>> steps;   // (mod, delta) in load order
    std::vector<TextAppend> appends;                     // $text: {description: !append ...}
    std::vector<std::pair<std::string, std::string>> diagrams;   // (item, diagram) of each $recipe
    std::vector<AlchemyMade> alchemy;                            // the recipes of $alchemy
    // the game's alchemy recipes for $alchemy, by recipe name and by the item they brew (read once when needed; the
    // normal game's copy before New Game Plus)
    std::map<std::string, AlchemyLike> like_by_recipe, like_by_item;
    bool likes_read = false;
    auto alchemy_like = [&](const std::string& key, AlchemyLike& out) {
        if (!likes_read) {
            likes_read = true;
            for (bool want_plus : {false, true})            // the first one of the normal game wins
                for (auto& kv : base) {
                    DefFile* f = kv.second;
                    if ((lower(f->path).find("_plus") != std::string::npos) != want_plus) continue;
                    records_of(*f, [&](Node*, Node* el) {
                        if (el->tag != "recipe") return;
                        const std::string* n = el->attr("name_name");
                        const std::string* c = el->attr("cookedItem_name");
                        const std::string* t = el->attr("cookedItemType");
                        if (!n || !c || !t) return;
                        AlchemyLike l{*n, *c, *t};
                        like_by_recipe.emplace(lower(*n), l);
                        like_by_item.emplace(lower(*c), l);
                    });
                }
        }
        for (auto* m : {&like_by_recipe, &like_by_item}) {
            auto it = m->find(lower(key));
            if (it != m->end()) { out = it->second; return true; }
        }
        return false;
    };
    std::vector<TextUse> text_uses;                      // $text fields, to keep the languages a YAML leaves out
    std::vector<AppearanceReq> appearances;              // $appearance (applied after all mods)
    std::vector<ModFile> mod_files;                      // <mod>\itemxl\files\..., the later mod wins
    // Every delta in load order, before the rules of the YAML files ($requires, $after, $if) are applied: those
    // need to know every mod and every new record first.
    struct Pending {
        std::string label, rel;
        Value doc;
        bool imported = false;
        Value requires_, after, cond;   // the document's $requires, $after, $if
    };
    std::vector<Pending> pending;
    std::map<std::string, std::vector<std::string>> overriders;   // lower definition file -> mods that replace it
    auto apply_yaml = [&](const std::string& label, const std::string& path, const std::string& rel) {
        Bytes b;
        if (!read_file(path, b)) { led.errors.push_back(label + ": " + rel + ": cannot read"); return; }
        std::string err;
        auto docs = yaml_load_all(text_of(b), err);
        if (!err.empty()) { led.errors.push_back(label + ": " + rel + ": " + err); return; }
        for (auto& d : docs) {
            Pending pd;
            pd.label = label;
            pd.rel = rel;
            Value body;
            body.type = Value::Dict;
            for (auto& kv : d.map) {
                if (kv.first == "$requires") pd.requires_ = kv.second;
                else if (kv.first == "$after") pd.after = kv.second;
                else if (kv.first == "$if") pd.cond = kv.second;
                else body.map.push_back(kv);
            }
            pd.doc = expand(body);
            pending.push_back(std::move(pd));
        }
    };

    for (auto& m : mods) {
        std::string dir = opt.mods_dir + "\\" + m;
        if (opt.auto_import) {
            std::vector<std::pair<std::string, Value>> per_file;
            for (auto& bf : scans[m].bundles) {
                const std::string& b = bf.path;
                for (auto& e : bundle_entries(b)) {
                    if (!is_definition_xml(e.name)) continue;
                    auto v = base.find(lower(e.name));
                    if (v == base.end()) continue;                 // a new file of the mod: not an override
                    if (mod_dlc_files.count(lower(e.name))) continue;   // its own DLC: read as copies above
                    overriders[lower(e.name)].push_back(m);
                    Bytes data;
                    std::string err;
                    std::unique_ptr<Node> root;
                    int dropped = 0;
                    if (bundle_read(e, data)) root = xml_parse(text_of(data), err, &dropped);
                    if (!root) { warn.push_back(b + ": " + e.name + " not parsed (" + err + ")"); continue; }
                    if (dropped)
                        warn.push_back(m + ": " + e.name + ": dropped " + std::to_string(dropped) +
                                       " duplicate attribute(s), kept the first of each");
                    DefFile mf;
                    mf.path = e.name;
                    mf.root = std::move(root);
                    std::vector<std::string> missing;
                    Value d = diff_files(*v->second, mf, &missing);
                    if (!missing.empty())
                        warn.push_back(m + ": " + e.name + " lacks " + std::to_string(missing.size()) +
                                       " vanilla record(s) (first: " + missing[0] +
                                       ") - kept as in the game (outdated whole-file override?)");
                    if (!d.map.empty()) per_file.emplace_back(v->second->path, d);
                }
            }
            Value delta = merge_file_deltas(per_file);
            if (!delta.map.empty()) {
                size_t n = 0;
                for (auto& kv : delta.map) n += kv.second.map.size();
                report.push_back("[import] " + m + ": " + std::to_string(n) + " records from whole-file overrides");
                Pending pd;
                pd.label = m;
                pd.doc = std::move(delta);
                pd.imported = true;
                pending.push_back(std::move(pd));
            }
        }
        std::string fdir = dir + "\\itemxl\\files";
        std::vector<std::string> own_files;
        for (auto& x : scans[m].xl) {
            const std::string& y = x.path;
            if (ends_with(lower(y), ".yaml") || ends_with(lower(y), ".yml"))
                apply_yaml(m, y, u8(fs::u8path(y).lexically_relative(fs::u8path(dir))));
            if (starts_with(lower(y), lower(fdir) + "\\")) own_files.push_back(y);
        }
        for (auto& f : own_files) {
            std::string rel = u8(fs::u8path(f).lexically_relative(fs::u8path(fdir)));
            std::replace(rel.begin(), rel.end(), '/', '\\');
            // not what ItemXL writes itself (a merged definition file, its DLC's own files), and no path the
            // bundle's name field cannot hold
            std::string lrel = lower(rel);
            if (index.files.count(lrel) || starts_with(lrel, lower(ITEMXL_DLC_ROOT) + "\\data\\gameplay\\") ||
                lrel == lower(ITEMXL_REDDLC_PATH)) {
                warn.push_back(m + ": itemxl\\files\\" + rel + " is a file ItemXL writes itself: ignored (change "
                               "records in a YAML file)");
                continue;
            }
            if (rel.size() > 255) {
                warn.push_back(m + ": itemxl\\files\\" + rel + ": the path is longer than 255 characters: ignored");
                continue;
            }
            for (auto& x : mod_files)   // two mods bring the same file: the later one wins, said in the report
                if (lower(x.path) == lower(rel))
                    warn.push_back(m + ": itemxl\\files\\" + rel + " replaces the file of " + x.mod);
            mod_files.erase(std::remove_if(mod_files.begin(), mod_files.end(),
                                           [&](const ModFile& x) { return lower(x.path) == lower(rel); }), mod_files.end());
            mod_files.push_back({m, rel, f});
        }
    }
    for (auto& y : global_yaml) {
        std::string rel = u8(fs::u8path(y).lexically_relative(fs::u8path(opt.game)));
        apply_yaml(rel, y, rel);
    }

    // --- the rules of the YAML files ---
    std::set<std::string> installed;                     // mods that are there and enabled
    for (auto& m : mods) installed.insert(lower(m));
    std::set<std::pair<std::string, std::string>> new_names;   // records some YAML makes ($base / $xml)
    for (auto& pd : pending)
        for (auto& t : pd.doc.map)
            if (t.second.is_map())
                for (auto& r : t.second.map)
                    if (r.second.is_map() && (r.second.has("$base") || r.second.has("$xml")))
                        new_names.insert({t.first, r.first.substr(0, r.first.find(" | "))});
    auto names_of = [](const Value& v) {
        std::vector<std::string> out;
        if (v.is_list()) for (auto& x : v.list) out.push_back(x.text());
        else if (!v.is_null()) out.push_back(v.text());
        return out;
    };
    // $if: {mod: X} installed and enabled; {item: X} / {ability: X} / ... the record exists (game or a YAML);
    // {not: ...}; {any: [...]}; a list or several keys: all of them
    std::function<bool(const Value&, std::string&)> holds = [&](const Value& c, std::string& err) -> bool {
        if (c.is_list()) {
            for (auto& x : c.list) if (!holds(x, err)) return false;
            return true;
        }
        if (!c.is_map()) { err = "$if must be a mapping"; return false; }
        for (auto& kv : c.map) {
            if (kv.first == "not") { if (holds(kv.second, err)) return false; }
            else if (kv.first == "any") {
                bool one = false;
                for (auto& x : kv.second.list) if (holds(x, err)) { one = true; break; }
                if (!one) return false;
            } else if (kv.first == "mod") {
                for (auto& n : names_of(kv.second)) if (!installed.count(lower(n))) return false;
            } else {
                for (auto& n : names_of(kv.second)) {
                    auto it = index.rec.find({kv.first, n});
                    bool there = (it != index.rec.end() && !it->second.empty()) || new_names.count({kv.first, n});
                    if (!there) return false;
                }
            }
            if (!err.empty()) return false;
        }
        return true;
    };
    // $after: [modX]: this mod's YAMLs come after modX's, so it wins where both set one value
    {
        std::vector<std::string> order;
        std::map<std::string, std::set<std::string>> after;
        for (auto& pd : pending) {
            if (std::find(order.begin(), order.end(), pd.label) == order.end()) order.push_back(pd.label);
            for (auto& n : names_of(pd.after)) after[pd.label].insert(lower(n));
        }
        bool moved = !after.empty();
        for (size_t round = 0; moved && round <= order.size(); round++) {
            moved = false;
            for (size_t i = 0; i < order.size(); i++) {
                auto it = after.find(order[i]);
                if (it == after.end()) continue;
                size_t last = i;
                for (size_t j = i + 1; j < order.size(); j++)
                    if (it->second.count(lower(order[j]))) last = j;
                if (last != i) {
                    std::string l = order[i];
                    order.erase(order.begin() + i);
                    order.insert(order.begin() + last, l);
                    moved = true;
                    break;
                }
            }
        }
        if (moved) warn.push_back("$after: the mods' $after lists contradict each other; the order is not changed further");
        std::stable_sort(pending.begin(), pending.end(), [&](const Pending& a, const Pending& b) {
            return std::find(order.begin(), order.end(), a.label) < std::find(order.begin(), order.end(), b.label);
        });
        for (auto& kv : after)
            for (auto& n : kv.second)
                for (auto& m : mods)
                    if (lower(m) == n) report.push_back("[after]  " + kv.first + " after " + m);
    }
    for (auto& pd : pending) {
        if (pd.imported) { steps.emplace_back(pd.label, std::move(pd.doc)); continue; }
        std::string where = pd.label + ": " + pd.rel;
        std::vector<std::string> missing;
        for (auto& n : names_of(pd.requires_)) if (!installed.count(lower(n))) missing.push_back(n);
        if (!missing.empty()) {
            std::string l;
            for (auto& n : missing) l += (l.empty() ? "" : ", ") + n;
            led.errors.push_back(where + ": needs " + l + " (not installed or disabled); this file is skipped");
            continue;
        }
        std::string err;
        if (!pd.cond.is_null() && !holds(pd.cond, err)) {
            if (!err.empty()) led.errors.push_back(where + ": " + err);
            else report.push_back("[if]     " + where + ": skipped, its $if does not hold");
            continue;
        }
        Value& doc = pd.doc;
        for (auto& t : doc.map) {
            if (!t.second.is_map()) continue;
            Map kept;
            for (auto& r : t.second.map) {
                Value* c = r.second.is_map() ? r.second.get("$if") : nullptr;
                if (c) {
                    std::string e;
                    bool ok = holds(*c, e);
                    if (!e.empty()) led.errors.push_back(where + ": " + t.first + " '" + r.first + "': " + e);
                    if (!ok) { report.push_back("[if]     " + where + ": " + t.first + " '" + r.first + "' skipped"); continue; }
                    r.second.map.erase(std::remove_if(r.second.map.begin(), r.second.map.end(),
                                                      [](auto& kv) { return kv.first == "$if"; }), r.second.map.end());
                }
                kept.push_back(std::move(r));
            }
            t.second.map = std::move(kept);
        }
        std::vector<std::string> sc_errors;
        Value extra = shortcuts(doc, prefix, &diagrams, alchemy_like, &alchemy, &sc_errors);
        for (auto& e : sc_errors) led.errors.push_back(pd.label + ": " + e);  // $sell / $loot / $recipe -> their own records
        for (const Value* dv : {&doc, &extra})
            for (auto& rid : touched(*dv)) { named.insert(rid); named_by.emplace(rid, pd.label); }
        take_texts(doc, texts, &appends, pd.label, &text_uses);
        take_texts(extra, texts, &appends, pd.label, &text_uses);
        take_appearances(doc, appearances, pd.label);
        size_t n = 0;
        for (auto& kv : doc.map) if (kv.second.is_map()) n += kv.second.map.size();
        report.push_back("[delta]  " + where + " (" + std::to_string(n) + " records)");
        steps.emplace_back(pd.label, std::move(doc));
        steps.emplace_back(pd.label, std::move(extra));
    }

    lap("yaml");
    // New records of all mods first, then the changes: the load order only matters where two mods set one value.
    std::vector<std::pair<std::string, Value>> made;
    for (auto& st : steps) made.emplace_back(st.first, split_new(index, st.second));
    lap("new");
    // A new record may be a copy ($base) of another mod's new record: that one is made first, whatever the load
    // order. Without such a link the records are made in load order.
    {
        struct NewRec { std::string mod, tag, key, name, base; Value spec; bool done = false; };
        std::vector<NewRec> fresh;
        std::set<std::pair<std::string, std::string>> waiting;
        for (auto& st : made)
            for (auto& t : st.second.map)
                for (auto& r : t.second.map) {
                    NewRec n{st.first, t.first, r.first, r.first.substr(0, r.first.find(" | ")), "", r.second};
                    if (const Value* b = r.second.get("$base")) n.base = b->text();
                    waiting.insert({n.tag, n.name});
                    fresh.push_back(std::move(n));
                }
        auto make = [&](NewRec& n) {
            Value recs, d;
            recs.set(n.key, std::move(n.spec));
            d.set(n.tag, std::move(recs));
            apply_delta(index, d, n.mod, led);
            n.done = true;
            waiting.erase({n.tag, n.name});
        };
        for (bool progress = true; progress;) {
            progress = false;
            for (auto& n : fresh) {
                if (n.done) continue;
                if (!n.base.empty() && n.base != n.name && waiting.count({n.tag, n.base})) continue;
                make(n);
                progress = true;
            }
        }
        for (auto& n : fresh) if (!n.done) make(n);          // a circle of $base: reported as "not found"
    }
    for (auto& st : steps)
        if (!st.second.map.empty()) apply_delta(index, st.second, st.first, led);
    lap("apply");
    // the texts the game knows: content\*\<lang>.w3strings, the DLC folders, and the mods' own .w3strings (the
    // later mod first); a language is read when needed
    std::map<std::string, std::vector<std::unique_ptr<W3Strings>>> game_texts;
    std::string dlc_root = opt.dlc_dir.empty() ? opt.game + "\\dlc" : opt.dlc_dir;
    auto game_text = [&](const std::string& lang, const std::string& key, std::string& out) {
        auto it = game_texts.find(lang);
        if (it == game_texts.end()) {
            auto& v = game_texts[lang];
            std::vector<std::string> files;
            for (auto it2 = mods.rbegin(); it2 != mods.rend(); ++it2)
                files.push_back(opt.mods_dir + "\\" + *it2 + "\\content\\" + lang + ".w3strings");
            for (auto& d : list_dirs(opt.game + "\\content"))
                files.push_back(opt.game + "\\content\\" + d + "\\" + lang + ".w3strings");
            for (auto& d : list_dirs(dlc_root))
                files.push_back(dlc_root + "\\" + d + "\\content\\" + lang + ".w3strings");
            for (auto& f : files) {
                auto s = std::make_unique<W3Strings>();
                if (s->load(f)) v.push_back(std::move(s));
            }
            it = game_texts.find(lang);
        }
        for (auto& s : it->second)
            if (s->by_key(key, out)) return true;
        return false;
    };
    if (!appends.empty()) append_texts(index, appends, texts, game_text, led);

    // $text on a record that had texts already: the languages a YAML leaves out keep the record's own text, so a
    // translation into one language does not take the others away. (A new record has no texts of its own: the
    // languages left out show the English text.)
    if (!text_uses.empty()) {
        static const char* LANGS[] = {"ar", "br", "cn", "cz", "de", "en", "es", "esmx", "fr", "hu", "it", "jp", "kr",
                                      "pl", "ru", "tr", "ua", "zh"};
        std::map<std::pair<std::string, std::string>, Node*> before;     // (tag, name) -> the record as read
        for (auto& kv : base)
            records_of(*kv.second, [&](Node*, Node* el) {
                auto k = key_of(*el);
                if (!k.second.empty()) before.emplace(std::make_pair(el->tag, k.second), el);
            });
        std::set<std::string> done;
        for (auto& u : text_uses) {
            if (!done.insert(u.key).second) continue;
            auto it = before.find({u.tag, u.name});
            if (it == before.end()) continue;                                  // a new record
            std::string attr = u.field == "name" ? "localisation_key_name"
                             : u.field == "description" ? "localisation_key_description" : u.field;
            const std::string* old = it->second->attr(attr);
            if (!old || old->empty()) continue;
            for (const char* l : LANGS) {
                if (texts[l].count(u.key)) continue;
                std::string t;
                if (game_text(l, *old, t)) texts[l][u.key] = t;
            }
        }
    }

    if (!diagrams.empty()) diagram_names(index, diagrams, texts, game_text, prefix);
    if (!alchemy.empty()) {
        // the game's recipe items with the kind they brew: the words their names start with
        std::map<std::string, std::string> type_of;                   // recipe name -> cookedItemType
        for (auto& kv : base) {
            if (lower(kv.second->path).find("_plus") != std::string::npos) continue;
            records_of(*kv.second, [&](Node*, Node* el) {
                const std::string* n = el->attr("name_name");
                const std::string* t = el->attr("cookedItemType");
                if (el->tag == "recipe" && n && t) type_of.emplace(*n, *t);
            });
        }
        std::vector<std::pair<std::string, std::string>> pages;
        std::set<std::string> seen;
        for (auto& kv : base) {
            if (lower(kv.second->path).find("_plus") != std::string::npos) continue;
            records_of(*kv.second, [&](Node*, Node* el) {
                const std::string* n = el->attr("name");
                const std::string* k = el->attr("localisation_key_name");
                if (el->tag != "item" || !n || !k || k->empty()) return;
                auto t = type_of.find(*n);
                if (t != type_of.end() && seen.insert(*n).second) pages.emplace_back(*k, t->second);
            });
        }
        alchemy_names(index, alchemy, texts, game_text, pages);
    }

    lap("mods");
    // $appearance: copies of templates, the mods' own models and textures (appearance.cpp)
    AppearanceOut look;
    if (!appearances.empty() || !mod_files.empty()) {
        std::map<std::string, std::pair<std::string, BundleEntry>> templates;   // lower name -> (path, entry)
        std::unordered_set<std::string> game_paths;                               // lower depot paths of the game
        bool scanned = false;
        auto scan = [&] {
            if (scanned) return;
            for (auto& entries : vanilla_entries)            // the game, its DLC and DLC folders; later ones win
                for (auto& e : entries) {
                    std::string n = lower(e.name);
                    game_paths.insert(n);
                    if (!ends_with(n, ".w2ent")) continue;
                    templates[n.substr(n.rfind('\\') + 1, n.size() - n.rfind('\\') - 7)] = {e.name, e};
                }
            scanned = true;
        };
        auto game_has = [&](const std::string& path) { scan(); return game_paths.count(lower(path)) > 0; };
        auto find_template = [&](const std::string& name, Bytes& data, std::string& path) {
            std::string key = lower(name);
            for (auto& mf : mod_files)                       // a mod's own template first
                if (ends_with(lower(mf.path), "\\" + key + ".w2ent") || lower(mf.path) == key + ".w2ent") {
                    path = mf.path;
                    return read_file(mf.disk, data);
                }
            scan();
            auto it = templates.find(key);
            if (it == templates.end()) return false;
            path = it->second.first;
            return bundle_read(it->second.second, data);
        };
        build_appearances(index, appearances, mod_files, find_template, game_has, look, led, warn);
        for (auto& r : appearances) report.push_back("[look]   " + r.mod + ": item " + r.item + " -> " + appearance_name(r.item));
        for (auto& mf : mod_files) report.push_back("[file]   " + mf.mod + ": " + mf.path);
    }

    lap("look");
    // ---- output ----------------------------------------------------------
    std::map<std::string, Bytes> out_files;
    std::vector<std::string> dirty;
    size_t yaml_warnings = 0;
    {   // a record has a copy for the game and one for New Game Plus: say each error once
        std::vector<std::string> once;
        for (auto& e : led.errors)
            if (std::find(once.begin(), once.end(), e) == once.end()) once.push_back(e);
        led.errors = std::move(once);
    }
    for (auto& w : led.warnings) { warn.push_back(w); yaml_warnings++; }   // from applying: $where, extend
    for (auto& w : check_new_items(index)) { warn.push_back(w); yaml_warnings++; }
    for (auto& w : validate(index, Catalog(base), named, named_by)) { warn.push_back(w); yaml_warnings++; }
    lap("check");

    // New items of the last build that this build no longer makes: saves that hold them lose them.
    std::set<std::string> made_now;
    for (auto& f : index.owned)
        records_of(*f, [&](Node*, Node* el) { if (el->tag == "item") made_now.insert(key_of(*el).second); });
    std::string items_path = opt.out_mod + "\\itemxl.items";
    Bytes old_items;
    if (read_file(items_path, old_items))
        for (auto& line : split(text_of(old_items), '\n')) {
            std::string n = trim(line);
            if (!n.empty() && n[0] != '#' && !made_now.count(n))
                warn.push_back("item '" + n + "' was made by the last build and is gone now (mod removed or item "
                               "renamed): saves that hold it lose it");
        }
    std::string items_body = "# new items of the last ItemXL build\n";
    for (auto& n : made_now) items_body += n + "\n";
    bool own_dlc = false;
    for (auto* store : {&work_store, &index.owned}) {
        for (auto& f : *store) {
            if (!f->dirty) continue;
            std::string s = serialize_def(*f);
            out_files[f->path] = Bytes(s.begin(), s.end());
            dirty.push_back(f->path);
            own_dlc |= starts_with(lower(f->path), lower(ITEMXL_DLC_ROOT) + "\\");
        }
    }
    // The game takes a file from the mod that wins in mods.settings. ItemXL's output is found first by its name
    // (mod00000_...) only among mods that are not listed there: a listed mod that replaces a file ItemXL writes
    // would win with its own, unmerged file.
    {
        auto self = ms.by_name.find(lower(OUTPUT_MOD));
        if (self != ms.by_name.end() && !self->second.enabled) {
            warn.push_back(std::string(OUTPUT_MOD) + " is disabled in mods.settings (Enabled=0): the game does not read "
                           "ItemXL's result");
            yaml_warnings++;
        }
        for (auto& bf : merged_bundles)
            for (auto& e : bundle_entries(bf.path))
                if (is_definition_xml(e.name)) overriders[lower(e.name)].push_back("mod0000_MergedFiles");
        std::set<std::string> told;
        for (auto& path : dirty) {
            auto ov = overriders.find(lower(path));
            if (ov == overriders.end()) continue;
            for (auto& m : ov->second) {
                auto e = ms.by_name.find(lower(m));
                if (e == ms.by_name.end() || !e->second.enabled) continue;   // not listed: ItemXL's name comes first
                if (self != ms.by_name.end() && self->second.priority <= e->second.priority) continue;
                if (!told.insert(m).second) continue;
                warn.push_back(m + " replaces " + path + " and is listed in mods.settings before " + OUTPUT_MOD +
                               ": the game reads that mod's file, not ItemXL's result. Add [" + OUTPUT_MOD +
                               "] with Enabled=1 and a Priority below every other mod's to mods.settings");
                yaml_warnings++;
            }
        }
    }
    for (auto& kv : look.files) {
        out_files[kv.first] = kv.second;
        own_dlc |= starts_with(lower(kv.first), lower(ITEMXL_DLC_ROOT) + "\\");
    }
    if (own_dlc) out_files[ITEMXL_REDDLC_PATH] = itemxl_reddlc(look.templates);   // mounts ItemXL's DLC folders

    // Stage the output into a temporary folder and swap it in at the end, so a failed or interrupted build does not
    // leave a half-written mod folder behind.
    std::string stage_mod = opt.out_mod + ".tmp";
    {
        std::error_code sec;
        fs::remove_all(fs::u8path(stage_mod), sec);
    }
    std::string content = stage_mod + "\\content";
    if (!look.textures.empty()) {
        Bytes tc = write_texture_cache(look.textures);
        make_dirs(content);
        if (!write_file(content + "\\texture.cache", tc.data(), tc.size())) {
            res.ok = false;
            warn.push_back("cannot write " + content + "\\texture.cache");
        }
        report.push_back("[tex]    " + std::to_string(look.textures.size()) + " texture(s) in " + std::string(OUTPUT_MOD) + "\\content\\texture.cache");
    }
    bool packed = false;
    if (!out_files.empty()) {
        std::string err;
        packed = write_content(content, out_files, err);
        if (!packed) { res.ok = false; warn.push_back(err); }
    }
    for (auto& lt : texts) {
        std::string p = stage_mod + "\\tw3se\\strings\\" + lt.first + ".txt";
        make_dirs(stage_mod + "\\tw3se\\strings");
        std::string body = "# generated by ItemXL\n";
        for (auto& kv : lt.second) {
            std::string t = kv.second;
            std::string esc;
            for (char c : t) {
                if (c == '\r') continue;                       // (a line of the text file ends with \n only)
                esc += c == '\n' ? std::string("\\n") : std::string(1, c);
            }
            body += kv.first + " = " + esc + "\n";
        }
        if (!write_file(p, body.data(), body.size())) { res.ok = false; warn.push_back("cannot write " + p); }
        report.push_back("[text]   " + std::string(OUTPUT_MOD) + "\\tw3se\\strings\\" + lt.first + ".txt");
    }

    // the script part: compiled by the game, shows missing items and build problems after a save is loaded
    {
        char id[32];
        std::snprintf(id, sizeof id, "%016llx", (unsigned long long)fnv1a64(stamp));
        std::string ws = itemxl_script(id, (int)led.errors.size(), (int)yaml_warnings);
        make_dirs(content + "\\scripts\\local");
        if (!write_file(content + "\\scripts\\local\\itemxl.ws", ws.data(), ws.size())) {
            res.ok = false;
            warn.push_back("cannot write " + content + "\\scripts\\local\\itemxl.ws");
        }
        report.push_back("[script] " + std::string(OUTPUT_MOD) + "\\content\\scripts\\local\\itemxl.ws");
    }
    make_dirs(stage_mod);
    if (!write_file(stage_mod + "\\itemxl.items", items_body.data(), items_body.size())) {
        res.ok = false;
        warn.push_back("cannot write " + items_path);
    }

    lap("write");
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::ostringstream r;
    char tbuf[32];
    std::snprintf(tbuf, sizeof tbuf, "%.1f", secs);
    r << "ItemXL " << VERSION << " build " << now_text() << "  (" << tbuf << "s)\n";
    if (!opt.environment.empty()) r << opt.environment << "\n";
    r << "\n";
    if (report.empty()) r << "(no deltas found)\n";
    for (auto& l : report) r << l << "\n";
    for (auto& l : led.wheres) r << "[where]  " << l << "\n";
    r << "\nfiles written: " << dirty.size() << "\n";
    for (auto& d : dirty) r << "  " << d << "\n";
    r << "\npacked: " << (packed ? opt.out_mod : std::string("no")) << "\n\n";
    r << "CONFLICTS: " << led.conflicts.size() << "\n";
    for (auto& c : led.conflicts)
        r << "  " << c.field << ": " << c.mod_a << " = '" << c.val_a << "'  vs  " << c.mod_b << " = '" << c.val_b
          << "'  -> " << c.mod_b << " wins\n";
    r << "\nERRORS: " << led.errors.size() << "\n";
    for (auto& e : led.errors) r << "  " << e << "\n";
    r << "\nWARNINGS: " << warn.size() << "\n";
    for (auto& w : warn) r << "  " << w << "\n";
    r << "\nVANILLA GAME FILES: " << game_notes.size() << "   (the game's own files, no mod causes these)\n";
    for (auto& g : game_notes) r << "  " << g << "\n";
    r << "\nstacked / overridden changes: " << led.notes.size() << "\n";
    for (auto& n : led.notes) r << "  " << n << "\n";

    res.built = true;
    res.files_written = (int)dirty.size();
    res.conflicts = (int)led.conflicts.size();
    res.errors = (int)led.errors.size();
    res.report = r.str();
    write_file(stage_mod + "\\itemxl_report.txt", res.report.data(), res.report.size());
    if (res.ok) write_file(stage_mod + "\\itemxl.stamp", stamp.data(), stamp.size());
    if (res.ok) {
        // The finished build goes into the output folder file by file, not by renaming folders: the folder keeps
        // its place (under a mod manager it may live in another mod than a new folder would), and a locked file
        // only fails that file instead of leaving no output at all. Files of an older build are removed after.
        std::error_code sec;
        auto stage = fs::u8path(stage_mod), dest = fs::u8path(opt.out_mod);
        std::set<std::string> fresh;
        std::vector<std::string> failed;
        for (auto& f : fs::recursive_directory_iterator(stage, sec)) {
            if (!f.is_regular_file(sec)) continue;
            auto rel = fs::relative(f.path(), stage, sec);
            fresh.insert(lower(rel.u8string()));
            auto to = dest / rel;
            fs::create_directories(to.parent_path(), sec);
            std::error_code ce;
            fs::copy_file(f.path(), to, fs::copy_options::overwrite_existing, ce);
            if (ce) failed.push_back(rel.u8string());
        }
        if (!failed.empty()) {
            res.ok = false;
            std::string msg = "cannot write " + std::to_string(failed.size()) + " file(s) of the output (first: " +
                              failed[0] + "); is the game or a tool holding them?";
            res.report += "\nERROR: " + msg + "\n";
            log(msg);
            fs::remove(dest / "itemxl.stamp", sec);       // so the next start builds again
        } else {
            std::vector<fs::path> stale;
            for (auto& f : fs::recursive_directory_iterator(dest, sec))
                if (f.is_regular_file(sec) && !fresh.count(lower(fs::relative(f.path(), dest, sec).u8string())))
                    stale.push_back(f.path());
            for (auto& p : stale) fs::remove(p, sec);
        }
        write_file(opt.out_mod + "\\itemxl_report.txt", res.report.data(), res.report.size());
        fs::remove_all(stage, sec);
    } else {
        std::error_code sec;
        fs::remove_all(fs::u8path(stage_mod), sec);
        make_dirs(opt.out_mod);
        write_file(opt.out_mod + "\\itemxl_report.txt", res.report.data(), res.report.size());
    }
    log("built: " + std::to_string(dirty.size()) + " files, " + std::to_string(led.conflicts.size()) +
        " conflicts, " + std::to_string(led.errors.size()) + " errors (" + tbuf + "s: " + laps + ")");
    return res;
}

}  // namespace itemxl
