// itemxl-cli.exe: the same core outside the game, for tests and for looking at a build without starting the game.
//
//   itemxl-cli build --game <W3> [--mods <Mods folder>] [--dlc <dlc folder>] [--settings <mods.settings>]
//                    [--out <mod folder>] [--force]
//   itemxl-cli import <.bundle> --game <W3>       the delta of a mod's whole-file overrides, as JSON
//   itemxl-cli yaml <file.yaml>                    a YAML file as JSON (to compare with PyYAML)
//   itemxl-cli reddlc <out> <id> <class> <path>...  a DLC definition (.reddlc)
//   itemxl-cli roundtrip <out folder>              every definition XML of the game read and written again
//   itemxl-cli unpack <.bundle> <out folder>       every file of a bundle (tests)
//   itemxl-cli pack <folder> <content folder>      a folder of loose files as blob0.bundle + metadata.store (tests)
//   itemxl-cli extract <out folder> <depot path>... files of the game's own bundles (tests)
//
// For writing YAML files (the game's own records, no mods):
//   itemxl-cli show <name> [--tag item] [--lang de]       a record as the game has it, every copy with its file
//   itemxl-cli search <text> [--tag item] [--lang de]     records whose name or in-game name contains the text
//   itemxl-cli new <name> --base <record> [--tag item] [--out file.yaml]   a YAML file to start a new record from
//   itemxl-cli schema [--out itemxl.schema.json]          JSON schema for VS Code: completion and checks while typing
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <cstring>
#include <iostream>
#include <string>

#include "browse.hpp"
#include "itemxl.hpp"

using namespace itemxl;

static std::string arg(int argc, char** argv, const char* name, const std::string& def = "") {
    for (int i = 1; i + 1 < argc; i++)
        if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
    return def;
}

// Every bundle of the game: <game>\content\*\**.bundle, then <game>\dlc\*\content\**.bundle, sorted.
static std::vector<std::string> game_bundles(const std::string& game) {
    std::error_code ec;
    std::vector<std::string> content_dirs, dlc_dirs;
    for (auto& d : std::filesystem::directory_iterator(std::filesystem::u8path(game + "\\content"), ec))
        if (d.is_directory(ec)) content_dirs.push_back(d.path().u8string());
    std::sort(content_dirs.begin(), content_dirs.end());
    for (auto& d : std::filesystem::directory_iterator(std::filesystem::u8path(game + "\\dlc"), ec))
        if (d.is_directory(ec)) dlc_dirs.push_back(d.path().u8string() + "\\content");
    std::sort(dlc_dirs.begin(), dlc_dirs.end());
    std::vector<std::string> roots = content_dirs, out;
    roots.insert(roots.end(), dlc_dirs.begin(), dlc_dirs.end());
    for (auto& r : roots) {
        std::vector<std::string> bundles;
        for (auto& f : std::filesystem::recursive_directory_iterator(std::filesystem::u8path(r), ec))
            if (f.is_regular_file(ec) && lower(f.path().extension().u8string()) == ".bundle")
                bundles.push_back(f.path().u8string());
        std::sort(bundles.begin(), bundles.end());
        out.insert(out.end(), bundles.begin(), bundles.end());
    }
    return out;
}

static const char* USAGE =
    "usage: itemxl-cli <command> [--game <game folder>]\n"
    "  search <text> [--tag item] [--lang de]\n"
    "  show <name> [--tag item] [--lang de]\n"
    "  new <name> --base <record> [--tag item] [--out file.yaml]\n"
    "  schema [--out itemxl.schema.json] [--vscode <folder>]\n"
    "  build [--mods <folder>] [--dlc <folder>] [--settings <mods.settings>] [--out <folder>] [--force]\n"
    "  import <.bundle>\n"
    "  retarget <in> <out> <old path>=<new path> ...\n";

// ---- the game folder, when --game is not given ----
static bool is_game(const std::string& dir) {
    std::error_code ec;
    return !dir.empty() && std::filesystem::is_directory(std::filesystem::u8path(dir + "\\content\\content0"), ec);
}

static std::string reg_text(HKEY root, const char* key, const char* value) {
    char buf[1024];
    DWORD size = sizeof buf - 1;
    if (RegGetValueA(root, key, value, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6432KEY, nullptr, buf, &size) != ERROR_SUCCESS &&
        RegGetValueA(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return "";
    buf[sizeof buf - 1] = 0;
    return buf;
}

// 1. a folder above this program (itemxl-cli inside the game folder); 2. GOG's registry entry of the game;
// 3. Steam: every library of steamapps\libraryfolders.vdf. "" when none of them has the game.
static std::string find_game() {
    wchar_t exe[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof exe / sizeof exe[0]));
    std::error_code ec;
    for (auto p = std::filesystem::path(std::wstring(exe, n)).parent_path(); !p.empty(); p = p.parent_path()) {
        if (is_game(p.u8string())) return p.u8string();
        if (p == p.root_path()) break;
    }
    for (const char* id : {"1495134320", "1207664663", "1207664643"}) {            // GOG: Game of the Year, base game
        std::string p = reg_text(HKEY_LOCAL_MACHINE, (std::string("SOFTWARE\\GOG.com\\Games\\") + id).c_str(), "path");
        if (is_game(p)) return p;
    }
    std::string steam = reg_text(HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "InstallPath");
    if (steam.empty()) steam = reg_text(HKEY_CURRENT_USER, "SOFTWARE\\Valve\\Steam", "SteamPath");
    std::vector<std::string> libs;
    if (!steam.empty()) libs.push_back(steam);
    Bytes vdf;
    if (!steam.empty() && read_file(steam + "\\steamapps\\libraryfolders.vdf", vdf)) {
        // lines like:   "path"   "D:\\SteamLibrary"
        for (auto& line : split(std::string(vdf.begin(), vdf.end()), '\n')) {
            size_t k = line.find("\"path\"");
            if (k == std::string::npos) continue;
            size_t a = line.find('"', k + 6), b = a == std::string::npos ? a : line.find('"', a + 1);
            if (b == std::string::npos) continue;
            std::string p;
            for (size_t i = a + 1; i < b; i++) {
                if (line[i] == '\\' && i + 1 < b && line[i + 1] == '\\') i++;      // a doubled backslash is one
                p += line[i];
            }
            libs.push_back(p);
        }
    }
    for (auto& l : libs) {
        std::string p = l + "\\steamapps\\common\\The Witcher 3";
        if (is_game(p)) return p;
    }
    return "";
}

static bool flag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; i++)
        if (std::strcmp(argv[i], name) == 0) return true;
    return false;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "%s", USAGE);
        return 2;
    }
    std::string cmd = argv[1];
    // the fewest arguments of the commands that read argv[2..] themselves
    static const std::map<std::string, int> NEEDS = {{"yaml", 3}, {"reddlc", 4}, {"roundtrip", 3}, {"cr2w", 4},
                                                     {"xbm", 7}, {"import", 3}, {"unpack", 4}, {"pack", 4},
                                                     {"extract", 4}, {"retarget", 5}};
    if (auto need = NEEDS.find(cmd); need != NEEDS.end() && argc < need->second) {
        std::fprintf(stderr, "%s: arguments missing\n%s", cmd.c_str(), USAGE);
        return 2;
    }
    std::string game = arg(argc, argv, "--game");
    if (game.empty()) game = find_game();
    static const std::set<std::string> NO_GAME = {"yaml", "reddlc", "unpack", "pack", "cr2w", "retarget", "xbm"};
    if (game.empty() && !NO_GAME.count(cmd)) {
        std::fprintf(stderr, "the game folder was not found; add --game \"<game folder>\"\n");
        return 2;
    }

    if (cmd == "yaml") {
        Bytes b;
        if (!read_file(argv[2], b)) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
        std::string err;
        auto docs = yaml_load_all(std::string(b.begin(), b.end()), err);
        if (!err.empty()) { std::printf("ERROR %s\n", err.c_str()); return 1; }
        std::string out = "[";
        for (size_t k = 0; k < docs.size(); k++) out += (k ? ", " : "") + to_json(docs[k]);
        std::printf("%s]\n", out.c_str());
        return 0;
    }

    if (cmd == "reddlc") {
        // reddlc <out file> <dlc id> <class> <path> [<class> <path> ...]: a DLC definition (tests compare it)
        std::vector<std::pair<std::string, std::string>> m;
        for (int i = 4; i + 1 < argc; i += 2) m.emplace_back(argv[i], argv[i + 1]);
        Bytes b = build_reddlc(argv[3], m);
        return write_file(argv[2], b.data(), b.size()) ? 0 : 1;
    }

    if (cmd == "roundtrip") {
        // every vanilla definition XML parsed and written again, into <out>\<depot path>; the written text read and
        // written once more must give the same text (a reader/writer pair that loses nothing)
        std::string out = argv[2];
        int n = 0, bad = 0, unstable = 0;
        for (auto& bpath : game_bundles(game)) {
            for (auto& e : bundle_entries(bpath)) {
                if (!is_definition_xml(e.name)) continue;
                Bytes data;
                std::string err;
                DefFile df;
                df.path = e.name;
                if (!bundle_read(e, data) || !is_definition_data(e.name, data)) continue;
                df.root = xml_parse(std::string(data.begin(), data.end()), err);
                if (!df.root) { std::printf("not parsed: %s (%s)\n", e.name.c_str(), err.c_str()); bad++; continue; }
                std::string s = serialize_def(df);
                DefFile again;
                again.path = e.name;
                again.root = xml_parse(s, err);
                if (!again.root || serialize_def(again) != s) { std::printf("not stable: %s\n", e.name.c_str()); unstable++; }
                std::string p = out + "\\" + e.name;
                make_dirs(p.substr(0, p.rfind('\\')));
                write_file(p, s.data(), s.size());
                n++;
            }
        }
        std::printf("%d files written, %d not parsed, %d not stable\n", n, bad, unstable);
        return bad || unstable ? 1 : 0;
    }

    if (cmd == "unpack" && argc >= 4) {
        int n = 0;
        for (auto& e : bundle_entries(argv[2])) {
            Bytes data;
            if (!bundle_read(e, data)) { std::fprintf(stderr, "cannot read %s\n", e.name.c_str()); return 1; }
            std::string p = std::string(argv[3]) + "\\" + e.name;
            make_dirs(p.substr(0, p.rfind('\\')));
            write_file(p, data.data(), data.size());
            n++;
        }
        std::printf("%d files\n", n);
        return n ? 0 : 1;
    }

    if (cmd == "pack" && argc >= 4) {
        std::map<std::string, Bytes> files;
        std::error_code ec;
        auto root = std::filesystem::u8path(argv[2]);
        for (auto& f : std::filesystem::recursive_directory_iterator(root, ec)) {
            if (!f.is_regular_file(ec)) continue;
            std::string rel = std::filesystem::relative(f.path(), root, ec).u8string();
            std::replace(rel.begin(), rel.end(), '/', '\\');
            Bytes b;
            if (read_file(f.path().u8string(), b)) files[rel] = std::move(b);
        }
        std::string err;
        if (files.empty() || !write_content(argv[3], files, err)) { std::fprintf(stderr, "pack: %s\n", files.empty() ? "no files" : err.c_str()); return 1; }
        std::printf("%zu files\n", files.size());
        return 0;
    }

    if (cmd == "extract" && argc >= 4) {
        // the first copy of each depot path in the game's bundle order (base game before DLC)
        std::map<std::string, std::string> wanted;
        for (int i = 3; i < argc; i++) {
            if (std::strcmp(argv[i], "--game") == 0) { i++; continue; }
            wanted[lower(argv[i])] = argv[i];
        }
        for (auto& bpath : game_bundles(game)) {
            for (auto& e : bundle_entries(bpath)) {
                auto w = wanted.find(lower(e.name));
                if (w == wanted.end()) continue;
                Bytes data;
                if (!bundle_read(e, data)) continue;
                std::string p = std::string(argv[2]) + "\\" + w->second;
                make_dirs(p.substr(0, p.rfind('\\')));
                write_file(p, data.data(), data.size());
                wanted.erase(w);
            }
            if (wanted.empty()) break;
        }
        for (auto& w : wanted) std::printf("not found: %s\n", w.second.c_str());
        return wanted.empty() ? 0 : 1;
    }

    if (cmd == "new") {
        std::string base = arg(argc, argv, "--base"), out = arg(argc, argv, "--out");
        if (argc < 3 || argv[2][0] == '-' || base.empty()) {
            std::fprintf(stderr, "usage: itemxl-cli new <name> --base <record> [--tag item] [--out file.yaml]\n");
            return 2;
        }
        GameData g;
        if (!g.load(game, arg(argc, argv, "--dlc", ""), arg(argc, argv, "--lang", "en"))) { std::fprintf(stderr, "no definition files under %s\n", game.c_str()); return 1; }
        std::string err, y = new_yaml(g, argv[2], base, arg(argc, argv, "--tag"), err);
        if (y.empty()) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        if (out.empty()) { std::printf("%s", y.c_str()); return 0; }
        Bytes old;
        if (read_file(out, old)) { std::fprintf(stderr, "%s exists, not changed\n", out.c_str()); return 1; }
        if (!write_file(out, y.data(), y.size())) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
        std::printf("written: %s\n", out.c_str());
        return 0;
    }

    if (cmd == "show" || cmd == "search" || cmd == "schema") {
        std::string tag = arg(argc, argv, "--tag"), lang = arg(argc, argv, "--lang", "en");
        std::string what = argc > 2 && argv[2][0] != '-' ? argv[2] : "";
        if (cmd != "schema" && what.empty()) { std::fprintf(stderr, "usage: itemxl-cli %s <text> [--tag item] [--lang de]\n", cmd.c_str()); return 2; }
        GameData g;
        if (!g.load(game, arg(argc, argv, "--dlc", ""), lang)) { std::fprintf(stderr, "no definition files under %s\n", game.c_str()); return 1; }
        if (cmd == "show") {
            std::string s = show(g, what, tag);
            if (s.empty()) { std::printf("no record '%s'%s (try: itemxl-cli search)\n", what.c_str(), tag.empty() ? "" : (" of " + tag).c_str()); return 1; }
            std::printf("%s", s.c_str());
            return 0;
        }
        if (cmd == "search") {
            auto found = search(g, what, tag);
            for (auto& f : found)
                std::printf("%-10s %-45s %-35s %-22s %s\n", f.tag.c_str(), f.name.c_str(), f.shown.c_str(),
                            f.category.c_str(), f.file.c_str());
            std::printf("%zu found\n", found.size());
            return 0;
        }
        // --vscode <folder>: the schema into <folder>\.vscode, and settings that use it for every itemxl\*.yaml there
        std::string vscode = arg(argc, argv, "--vscode");
        std::string out = arg(argc, argv, "--out", vscode.empty() ? "itemxl.schema.json" : vscode + "\\.vscode\\itemxl.schema.json");
        if (!vscode.empty()) make_dirs(vscode + "\\.vscode");
        std::string s = schema(g);
        if (!write_file(out, s.data(), s.size())) { std::fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
        std::printf("written: %s (%zu KB)\n", out.c_str(), s.size() / 1024);
        if (!vscode.empty()) {
            std::string fwd = std::filesystem::absolute(std::filesystem::u8path(out)).u8string();
            std::replace(fwd.begin(), fwd.end(), '\\', '/');
            std::string st = "{\n  \"yaml.schemas\": {\"" + fwd + "\": [\"**/itemxl/*.yaml\", \"**/itemxl/*.yml\"]},\n"
                             "  \"yaml.customTags\": [";
            bool first = true;
            for (const char* t : {"append", "append-once", "prepend", "prepend-once", "remove", "set", "append-from", "prepend-from"})
                for (const char* kind : {"scalar", "sequence", "mapping"}) {
                    st += std::string(first ? "" : ", ") + "\"!" + t + " " + kind + "\"";
                    first = false;
                }
            st += ", \"!scale scalar\", \"!offset scalar\"],\n  \"yaml.maxItemsComputed\": 20000\n}\n";
            std::string sp = vscode + "\\.vscode\\settings.json";
            Bytes old;
            if (read_file(sp, old)) {
                std::printf("%s exists, not changed. Add this to it:\n%s", sp.c_str(), st.c_str());
            } else {
                write_file(sp, st.data(), st.size());
                std::printf("written: %s\n", sp.c_str());
            }
        }
        return 0;
    }

    if (cmd == "cr2w") {
        // cr2w <in> <out>: read and write a resource again (tests: the same bytes)
        Bytes b;
        std::string err;
        Cr2w f;
        if (!read_file(argv[2], b) || !f.read(b, err)) { std::fprintf(stderr, "cannot read %s %s\n", argv[2], err.c_str()); return 1; }
        Bytes o = f.write();
        return write_file(argv[3], o.data(), o.size()) ? 0 : 1;
    }

    if (cmd == "retarget" && argc >= 5) {
        // retarget <in> <out> <old path>=<new path> ...: a resource whose imports point elsewhere (also in an
        // entity template's flatCompiledData); for mod authors who make their own template from the game's
        Bytes b;
        std::string err;
        if (!read_file(argv[2], b)) { std::fprintf(stderr, "cannot read %s\n", argv[2]); return 1; }
        std::map<std::string, std::string> paths;
        for (int i = 4; i < argc; i++) {
            std::string a = argv[i];
            size_t eq = a.find('=');
            if (eq == std::string::npos) { std::fprintf(stderr, "not old=new: %s\n", a.c_str()); return 2; }
            paths[lower(a.substr(0, eq))] = a.substr(eq + 1);
        }
        int changed = 0;
        Bytes o = cr2w_retarget(b, paths, changed, err);
        if (o.empty()) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        std::printf("%d import(s) changed\n", changed);
        return write_file(argv[3], o.data(), o.size()) ? 0 : 1;
    }

    if (cmd == "xbm") {
        // xbm <in.dds> <depot name.xbm> <cache key> <out.xbm> <out texture.cache> [<texture group>]: a texture as
        // ItemXL makes it (tests: the game's .xbm from the game's pixels)
        Bytes b, pixels;
        std::string err;
        TextureInfo info;
        if (!read_file(argv[2], b) || !read_dds(b, argv[3], info, pixels, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
        uint32_t key = (uint32_t)std::strtoul(argv[4], nullptr, 10);
        if (argc > 7) info.group = argv[7];
        auto pieces = texture_pieces(info, pixels);
        Bytes x = make_xbm(info, key, pieces.back());
        Bytes c = write_texture_cache({{argv[3], key, info, pieces}});
        return write_file(argv[5], x.data(), x.size()) && write_file(argv[6], c.data(), c.size()) ? 0 : 1;
    }

    if (cmd == "build") {
        BuildOptions opt;
        opt.game = game;
        opt.mods_dir = arg(argc, argv, "--mods", game + "\\Mods");
        opt.mods_settings = arg(argc, argv, "--settings", "");
        opt.out_mod = arg(argc, argv, "--out", opt.mods_dir + "\\mod00000_ItemXL");
        opt.dlc_dir = arg(argc, argv, "--dlc", "");
        opt.force = flag(argc, argv, "--force");
        opt.auto_import = !flag(argc, argv, "--no-auto-import");
        opt.log = [](const std::string& m) { std::fprintf(stderr, "[ItemXL] %s\n", m.c_str()); };
        BuildResult r = build(opt);
        if (r.built) std::printf("%s", r.report.c_str());
        return r.ok ? 0 : 1;
    }

    if (cmd == "import") {
        // vanilla files of the game, then the diff of each overridden file in the bundle
        std::map<std::string, std::unique_ptr<DefFile>> vanilla;
        std::string bpath = argv[2];
        std::vector<std::pair<std::string, Value>> per_file;
        // read the vanilla XMLs the bundle overrides
        std::map<std::string, std::string> wanted;
        for (auto& e : bundle_entries(bpath))
            if (is_definition_xml(e.name)) wanted[lower(e.name)] = e.name;
        std::error_code ec;
        std::vector<std::string> roots;
        for (auto& d : std::filesystem::directory_iterator(std::filesystem::u8path(game + "\\content"), ec))
            roots.push_back(d.path().u8string());
        std::sort(roots.begin(), roots.end());
        for (auto& r : roots)
            for (auto& f : std::filesystem::recursive_directory_iterator(std::filesystem::u8path(r), ec)) {
                if (lower(f.path().extension().u8string()) != ".bundle") continue;
                for (auto& e : bundle_entries(f.path().u8string())) {
                    if (!wanted.count(lower(e.name))) continue;
                    Bytes data;
                    std::string err;
                    if (!bundle_read(e, data)) continue;
                    auto root = xml_parse(std::string(data.begin(), data.end()), err);
                    if (!root) continue;
                    auto df = std::make_unique<DefFile>();
                    df->path = e.name;
                    df->root = std::move(root);
                    vanilla[lower(e.name)] = std::move(df);
                }
            }
        for (auto& e : bundle_entries(bpath)) {
            auto v = vanilla.find(lower(e.name));
            if (v == vanilla.end()) continue;
            Bytes data;
            std::string err;
            if (!bundle_read(e, data)) continue;
            DefFile mf;
            mf.path = e.name;
            mf.root = xml_parse(std::string(data.begin(), data.end()), err);
            if (!mf.root) continue;
            Value d = diff_files(*v->second, mf);
            if (!d.map.empty()) per_file.emplace_back(v->second->path, d);
        }
        std::printf("%s\n", to_json(merge_file_deltas(per_file)).c_str());
        return 0;
    }
    std::fprintf(stderr, "unknown command %s\n%s", cmd.c_str(), USAGE);
    return 2;
}
