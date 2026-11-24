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
//   itemxl reload     build now and put what changed into the running game (see live changes below)
//   itemxl live on|off  watch the mods' itemxl folders while the game runs (on by default, for this session)
//
// Live changes (TW3SE 1.4.0 and later): a background thread looks at the YAML files in the mods' itemxl folders
// (and <game>\bin\itemxl) about once a second (see watch_loop for what keeps it cheap). When one changed and stays the same for a moment, it builds in memory, compares
// the result with the output the game loaded (live.cpp) and hands the changed records to the main thread, which
// mounts them as one definitions XML through TW3SE's Lua (w3.files.set, w3.items.mount). Nothing on disk changes
// but the report and the text files; the stamp stays old, so the next start builds the output again as usual.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "browse.hpp"
#include "itemxl.hpp"
#include "tw3se/tw3se.h"
#include "version.hpp"

static const TW3SE_Interface* g_api = nullptr;
static std::string g_summary = "ItemXL has not run yet";
static std::string g_report_path;
static std::string g_env;
static std::string g_game, g_out_mod;
static itemxl::BuildOptions g_opt;          // the options of the start's build, for live builds

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

// ---- live changes ------------------------------------------------------------------------------------------------

namespace fs = std::filesystem;

static std::thread* g_watch = nullptr;   // never destroyed: a joinable std::thread at DLL unload would end the game
static std::atomic<bool> g_stop{false}, g_live_on{true}, g_reload{false}, g_again{false};
static std::mutex g_mx;                      // guards the members below
static std::vector<std::string> g_logs;      // lines from the thread, logged on the main thread
static itemxl::LiveDelta g_mount;            // the delta the main thread mounts next (empty: none waiting)
static bool mount_waiting() { return !g_mount.xml.empty() || !g_mount.xml_plus.empty(); }
static int g_mount_count = 0;
static std::map<std::string, itemxl::Bytes> g_loaded;   // the output as the game has it now
static std::map<std::string, itemxl::Bytes> g_pending;  // the output of the build whose delta waits to be mounted
static bool g_loaded_read = false;

static void flush_logs(void*) {
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        lines.swap(g_logs);
    }
    for (auto& l : lines) log_line(l);
}

static void live_log(const std::string& m) {
    {
        std::lock_guard<std::mutex> lk(g_mx);
        g_logs.push_back(m);
    }
    g_api->RunOnMainThread(&flush_logs, nullptr);
}

// The watching must never be felt in the game, with any number of mods. So the thread lists no folder at all while
// the game runs: it watches exactly the YAML files the start's build read (BuildResult::yaml_files) and asks once a
// second for each one's size and date, one call per file. That is a few dozen calls a second in a thread of the
// lowest priority, also under a mod manager's virtual file system. A new YAML file or a new mod is seen at the next
// start or with /itemxl reload, which lists the folders once. The builds the thread starts run in it as well.
// As a safety net each look is timed; should one ever take longer than LOOK_LIMIT, the watching stops for the
// session and the log says so (/itemxl reload still works).
static const auto LOOK_LIMIT = std::chrono::milliseconds(100);
static std::vector<std::wstring> g_yaml;     // the watched files; set before the thread starts, then its own

static std::wstring wide(const std::string& s) { return fs::u8path(s).wstring(); }

// size and date of each YAML file: one call per file, no folder listing
static std::string signature(const std::vector<std::wstring>& files) {
    std::string sig;
    sig.reserve(files.size() * 32);
    for (auto& f : files) {
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExW(f.c_str(), GetFileExInfoStandard, &a)) { sig += "-\n"; continue; }
        char b[64];
        std::snprintf(b, sizeof b, "%lu.%lu.%lu.%lu\n", a.nFileSizeLow, a.nFileSizeHigh, a.ftLastWriteTime.dwLowDateTime,
                      a.ftLastWriteTime.dwHighDateTime);
        sig += b;
    }
    return sig;
}

// Lua asks for an XML to mount: its bytes as they are ("plus": the New Game Plus one)
static int lua_delta(int argc, const char* const* argv, char* reply, uint32_t size, void*) {
    std::lock_guard<std::mutex> lk(g_mx);
    const std::string& x = argc > 0 && std::strcmp(argv[0], "plus") == 0 ? g_mount.xml_plus : g_mount.xml;
    if (x.empty()) { std::snprintf(reply, size, "%s", ""); return 1; }
    g_api->LuaReplyBytes(x.data(), x.size());
    return 1;
}

// Lua run on the main thread: mounts the waiting XMLs as files of their own (gameplay\items exists in the depot, so
// new files there are allowed). Earlier mounts stay: unmounting would take their records out of the game.
// The New Game Plus XML goes through the game's New Game Plus mounter, the way TW3SE's w3.items.mount does it for
// the normal one (vf46 loads, +0x7c says loaded), and only in a New Game Plus game (the game's own test, the fact
// NewGamePlus). Its mounters are kept in a global table so they live. Answer: "<normal>;<plus>", each ok, none,
// skip (no New Game Plus game) or the reason.
static const char* MOUNT_LUA = R"LUA(
local n, normal, plus = ...
if not (w3.items and w3.items.mount and w3.files and w3.files.set) then return 'TW3SE 1.4.0 or later is needed;none' end
local a, b = 'none', 'none'
if normal then
  local p = [[gameplay\items\itemxl_live_]] .. n .. '.xml'
  local ok, e = w3.files.set(p, w3.plugin.itemxl_delta())
  if not ok then a = 'files.set: ' .. tostring(e)
  else
    local m, e2 = w3.items.mount(p)
    a = m and 'ok' or ('mount: ' .. tostring(e2))
  end
end
if plus then
  local ngp = (w3.call('FactsQuerySum', 'NewGamePlus') or 0) > 0
  if not ngp then b = 'skip'
  else
    local p = [[gameplay\items\itemxl_live_plus_]] .. n .. '.xml'
    local ok, e = w3.files.set(p, w3.plugin.itemxl_delta('plus'))
    if not ok then b = 'files.set: ' .. tostring(e)
    else
      local m = w3.new('CR4DefinitionsNGPlusDLCMounter')
      m.definitionXmlFilePath = p
      w3.engine_call('CR4DefinitionsNGPlusDLCMounter::vf46', 'p>v', m)
      if ((w3.peek(w3.addr(m) + 0x7c) or 0) & 0xff) == 0 then w3.discard(m) b = 'not loaded by the New Game Plus mounter'
      else ITEMXL_LIVE_MOUNTS = ITEMXL_LIVE_MOUNTS or {} table.insert(ITEMXL_LIVE_MOUNTS, m) b = 'ok' end
    end
  end
end
return a .. ';' .. b
)LUA";

static void mount_delta(void*) {
    itemxl::LiveDelta d;
    int n;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        if (!mount_waiting()) return;
        d = g_mount;
        n = ++g_mount_count;
    }
    // RunLua takes a chunk: the values go in front as locals
    std::string code = "return (function(...) " + std::string(MOUNT_LUA) + " end)(" + std::to_string(n) + ", " +
                       (d.records ? "true" : "false") + ", " + (d.records_plus ? "true" : "false") + ")";
    char reply[512] = {0};
    int ran = g_api->RunLua(code.c_str(), reply, sizeof reply);
    std::string r = reply;
    while (!r.empty() && (r.back() == '\n' || r.back() == '\r' || r.back() == ' ')) r.pop_back();
    size_t semi = r.find(';');
    std::string a = ran && semi != std::string::npos ? r.substr(0, semi) : r;
    std::string b = ran && semi != std::string::npos ? r.substr(semi + 1) : "";
    bool ok = ran && (a == "ok" || a == "none");
    std::lock_guard<std::mutex> lk(g_mx);
    if (ok) {
        g_loaded = std::move(g_pending);
        if (a == "ok") {
            log_line("live: " + std::to_string(d.records) + " record(s) loaded into the game (itemxl_live_" +
                     std::to_string(n) + ".xml)");
            for (auto& nm : d.names) log_line("live:   " + nm);
        }
        if (b == "ok") {
            log_line("live: " + std::to_string(d.records_plus) + " New Game Plus record(s) loaded");
            for (auto& nm : d.names_plus) log_line("live:   " + nm);
        } else if (b == "skip") {
            log_line("live: " + std::to_string(d.records_plus) + " New Game Plus record(s) left out: this is no New "
                     "Game Plus game");
        } else if (b != "none") {
            log_line("live: New Game Plus records not loaded (take effect at the next start): " + b);
        }
        // the notice counts each name once: an item changed for the game and for New Game Plus is one entry
        std::vector<std::string> shown;
        auto add = [&](const std::string& n) {
            if (std::find(shown.begin(), shown.end(), n) == shown.end()) shown.push_back(n);
        };
        if (a == "ok") for (auto& k : d.keys) add(k.second);
        if (b == "ok") for (auto& k : d.keys_plus) add(k.second);
        if (!shown.empty()) {
            std::string text = shown.size() == 1 ? "ItemXL: " + shown[0] + " updated"
                                                 : "ItemXL: " + std::to_string(shown.size()) + " entries updated";
            g_api->RunLua(("if w3.hud and w3.hud.message then pcall(w3.hud.message, [==[" + text +
                           "]==], 3) end").c_str(), reply, sizeof reply);
            g_summary = "ItemXL: " + std::to_string(shown.size()) + " record(s) changed while the game runs";
        }
        // for other mods (w3.on_message("itemxl.live", fn) in Lua, RegisterOnMessage in a plugin, TW3SE_Receive in a
        // script): one line per record, "<tag><TAB><name>"
        if (a == "ok" && g_api->interfaceVersion >= 21) {
            std::string msg;
            for (auto& k : d.keys) msg += k.first + "\t" + k.second + "\n";
            g_api->SendToChannel("itemxl.live", msg.c_str());
        }
    } else {
        // g_loaded stays: the next change sends these records again
        log_line("live: not loaded: " + a);
    }
    g_pending.clear();
    g_mount = itemxl::LiveDelta();
}

static void live_build() {
    itemxl::BuildOptions opt = g_opt;
    opt.live = opt.force = true;
    opt.log = live_log;
    itemxl::BuildResult r;
    try {
        r = itemxl::build(opt);
    } catch (const std::exception& e) {
        live_log(std::string("live: build failed: ") + e.what());
        return;
    }
    std::lock_guard<std::mutex> lk(g_mx);
    if (mount_waiting()) {                       // the last one is not mounted yet: build again after it
        g_again = true;
        return;
    }
    if (!g_loaded_read) {
        g_loaded = itemxl::bundle_files(g_opt.out_mod + "\\content\\blob0.bundle");
        g_loaded_read = true;
    }
    itemxl::LiveDelta d = itemxl::live_delta(g_loaded, r.files);
    for (auto& n : d.notes) g_logs.push_back("live: " + n);
    if (r.errors) g_logs.push_back("live: " + std::to_string(r.errors) + " error(s), see the report");
    if (!d.records && !d.records_plus) {
        g_logs.push_back("live: no record changed");
        g_api->RunOnMainThread(&flush_logs, nullptr);
        return;
    }
    g_mount = std::move(d);
    g_pending = std::move(r.files);
    g_api->RunOnMainThread(&flush_logs, nullptr);
    g_api->RunOnMainThread(&mount_delta, nullptr);
}

// /itemxl reload: the YAML files as the folders have them now (the one time the thread lists folders)
static std::vector<std::wstring> list_yaml() {
    std::vector<std::wstring> out;
    std::error_code ec;
    std::vector<fs::path> folders;
    for (auto& d : fs::directory_iterator(fs::u8path(g_opt.mods_dir), ec)) {
        std::error_code de;
        if (d.is_directory(de) && fs::is_directory(d.path() / "itemxl", de)) folders.push_back(d.path() / "itemxl");
    }
    folders.push_back(fs::u8path(g_opt.game + "\\bin\\itemxl"));
    for (auto& f : folders) {
        std::error_code fe;
        for (auto it = fs::recursive_directory_iterator(f, fe); !fe && it != fs::recursive_directory_iterator();
             it.increment(fe)) {
            std::wstring e = it->path().extension().wstring();
            for (auto& c : e) c = (wchar_t)towlower(c);
            std::error_code re;
            if ((e == L".yaml" || e == L".yml") && it->is_regular_file(re)) out.push_back(it->path().wstring());
        }
    }
    return out;
}

static void watch_loop() {
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);
    std::string seen = signature(g_yaml), last = seen;
    while (!g_stop) {
        // a second, in short steps so /itemxl reload and the end of the plugin are answered at once
        for (int i = 0; i < 10 && !g_stop && !g_reload; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (g_stop) break;
        if (g_reload.exchange(false)) {
            g_yaml = list_yaml();
            seen = last = signature(g_yaml);
            live_build();
            continue;
        }
        if (g_again.exchange(false)) {               // a change came while the one before was being mounted
            live_build();
            continue;
        }
        if (!g_live_on) continue;
        auto t0 = std::chrono::steady_clock::now();
        std::string now = signature(g_yaml);
        auto took = std::chrono::steady_clock::now() - t0;
        if (took > LOOK_LIMIT) {
            g_live_on = false;
            live_log("live: watching off for this session, a look at " + std::to_string(g_yaml.size()) +
                     " YAML file(s) took " +
                     std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(took).count()) +
                     " ms; /itemxl reload still loads changes");
            continue;
        }
        // build once the files stay the same for a second (an editor may save in steps)
        if (now != seen && now == last) {
            seen = now;
            live_build();
        }
        last = now;
    }
}

static void cmd_itemxl(const char* args, char* reply, uint32_t size) {
    std::string a = args ? itemxl::trim(args) : "";
    std::string out;
    if (a.empty()) out = g_summary;
    else if (a == "version") out = "ItemXL " ITEMXL_VERSION ", " + g_env;
    else if (a == "report") out = report_excerpt();
    else if (a == "reload") {
        if (!g_watch) out = "live changes need TW3SE 1.4.0 or later";
        else { g_reload = true; out = "ItemXL builds now; the TW3SE log says what was loaded"; }
    } else if (a == "live on" || a == "live off") {
        g_live_on = a == "live on";
        out = std::string("live changes ") + (g_live_on ? "on" : "off") + " for this session";
    }
    else if (itemxl::starts_with(a, "show ")) {
        // reads the game's definitions and ItemXL's build over them (a few seconds)
        std::string name = itemxl::trim(a.substr(5));
        itemxl::GameData g;
        g.load(g_game, "", "");
        g.overlay(g_out_mod + "\\content\\blob0.bundle");
        out = itemxl::show(g, name);
        if (out.empty()) out = "no record '" + name + "'";
    } else out = "usage: itemxl [version|report|show <name>|reload|live on|live off]";
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
    api->RegisterCommand("itemxl", &cmd_itemxl,
                         "ItemXL: summary | version | report | show <name> | reload | live on|off");
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
        // TW3SE's own items (w3.items), written by TW3SE 1.4.0 and later
        opt.tw3se_items = opt.game + "\\bin\\x64_dx12\\tw3se\\files\\dlc\\dlctw3se\\data\\gameplay\\items\\tw3se_items.xml";
        opt.log = log_line;
        g_report_path = opt.out_mod + "\\itemxl_report.txt";
        g_game = opt.game;
        g_out_mod = opt.out_mod;
        g_opt = opt;
        log_line("game: " + opt.game);
        itemxl::BuildResult r = itemxl::build(opt);
        for (auto& y : r.yaml_files) g_yaml.push_back(wide(y));
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
    // live changes: LuaReplyBytes came with interface 20, w3.items.mount with TW3SE 1.4.0 (checked when mounting)
    if (api->interfaceVersion >= 20 && !g_opt.mods_dir.empty()) {
        api->RegisterLuaFunction("itemxl_delta", &lua_delta, nullptr);
        g_watch = new std::thread(watch_loop);
    }
    return 1;   // stay loaded even after a failed build: the game starts with the last good output
}

// `plugin reload ItemXL`: the thread ends before the DLL goes
TW3SE_EXPORT int TW3SE_PluginUnload(void) {
    g_stop = true;
    if (g_watch && g_watch->joinable()) g_watch->join();
    delete g_watch;
    g_watch = nullptr;
    return 0;
}
