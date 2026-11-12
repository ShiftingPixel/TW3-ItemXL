// ItemXL as a TW3SE plugin: ItemXL.dll in <game>\bin\x64_dx12\tw3se\plugins\.
//
// TW3SE calls TW3SE_PluginLoad on the game's main thread before the engine starts up, so the build below is done
// before the game reads its mods. When nothing changed since the last start (itemxl.stamp), it takes a few
// milliseconds; otherwise it rebuilds <game>\Mods\mod00000_ItemXL.
//
// Console command (TW3SE console / pipe), like TweakXL.Version() and friends:
//   itemxl            summary of the last build
//   itemxl version    ItemXL, TW3SE and game build
//   itemxl report     the start of itemxl_report.txt (conflicts and errors first)
//   itemxl show <name>  a record as the game reads it now: the game's files with ItemXL's build over them
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

#include "browse.hpp"
#include "itemxl.hpp"
#include "tw3se/tw3se.h"
#include "version.hpp"

static const TW3SE_Interface* g_api = nullptr;
static std::string g_summary = "ItemXL has not run yet";
static std::string g_report_path;
static std::string g_env;
static std::string g_game, g_out_mod;

static std::string utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// <game>\bin\x64_dx12\witcher3.exe -> <game>
static std::string game_folder() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof buf / sizeof buf[0]));
    std::wstring p(buf, n);
    for (int up = 0; up < 3; up++) {
        size_t s = p.find_last_of(L"\\/");
        if (s == std::wstring::npos) return {};
        p.resize(s);
    }
    return utf8(p);
}

static std::string documents_folder() {
    PWSTR path = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &path))) out = utf8(path);
    CoTaskMemFree(path);
    return out;
}

static void log_line(const std::string& m) {
    if (g_api) g_api->Log("ItemXL", "%s", m.c_str());
}

// The report with its CONFLICTS / ERRORS / WARNINGS parts first, cut to the reply size.
static std::string report_excerpt() {
    itemxl::Bytes b;
    if (g_report_path.empty() || !itemxl::read_file(g_report_path, b)) return "no report yet";
    std::string text(b.begin(), b.end());
    size_t c = text.find("CONFLICTS:");
    return c == std::string::npos ? text : text.substr(c);
}

static void cmd_itemxl(const char* args, char* reply, uint32_t size) {
    std::string a = args ? itemxl::trim(args) : "";
    std::string out;
    if (a.empty()) out = g_summary;
    else if (a == "version") out = "ItemXL " ITEMXL_VERSION ", " + g_env;
    else if (a == "report") out = report_excerpt();
    else if (itemxl::starts_with(a, "show ")) {
        // reads the game's definitions and ItemXL's build over them (a few seconds)
        std::string name = itemxl::trim(a.substr(5));
        itemxl::GameData g;
        g.load(g_game, "", "");
        g.overlay(g_out_mod + "\\content\\blob0.bundle");
        out = itemxl::show(g, name);
        if (out.empty()) out = "no record '" + name + "'";
    } else out = "usage: itemxl [version|report|show <name>]";
    std::snprintf(reply, size, "%s", out.c_str());
}

TW3SE_EXPORT int TW3SE_PluginQuery(TW3SE_PluginInfo* info) {
    info->name = "ItemXL";
    info->author = "ItemXL";
    info->version = (ITEMXL_VERSION_MAJOR << 16) | (ITEMXL_VERSION_MINOR << 8) | ITEMXL_VERSION_PATCH;
    info->minInterface = 1;       // Log, RegisterCommand
    return 1;
}

TW3SE_EXPORT int TW3SE_PluginLoad(const TW3SE_Interface* api) {
    g_api = api;
    api->RegisterCommand("itemxl", &cmd_itemxl, "ItemXL: summary | version | report | show <name>");
    char env[128];
    std::snprintf(env, sizeof env, "TW3SE %u.%u.%u (interface %u), game build %08x", api->extenderVersion >> 16,
                  (api->extenderVersion >> 8) & 0xFF, api->extenderVersion & 0xFF, api->interfaceVersion,
                  api->gameBuild);
    g_env = env;
    log_line("ItemXL " ITEMXL_VERSION ", " + g_env);
    try {
        itemxl::BuildOptions opt;
        opt.game = game_folder();
        opt.mods_dir = opt.game + "\\Mods";
        std::string docs = documents_folder();
        opt.mods_settings = docs.empty() ? "" : docs + "\\The Witcher 3\\mods.settings";
        opt.out_mod = opt.mods_dir + "\\mod00000_ItemXL";
        opt.environment = g_env;
        opt.log = log_line;
        g_report_path = opt.out_mod + "\\itemxl_report.txt";
        g_game = opt.game;
        g_out_mod = opt.out_mod;
        log_line("game: " + opt.game);
        itemxl::BuildResult r = itemxl::build(opt);
        if (!r.built) {
            g_summary = "ItemXL: nothing changed since the last start";
        } else {
            char buf[256];
            std::snprintf(buf, sizeof buf, "ItemXL: %d files merged, %d conflicts, %d errors. Report: %s",
                          r.files_written, r.conflicts, r.errors, g_report_path.c_str());
            g_summary = buf;
        }
        log_line(g_summary);
    } catch (const std::exception& e) {
        g_summary = std::string("ItemXL failed: ") + e.what();
        log_line(g_summary);
    } catch (...) {
        g_summary = "ItemXL failed with an unknown error";
        log_line(g_summary);
    }
    return 1;   // stay loaded even after a failed build: the game starts with the last good output
}
