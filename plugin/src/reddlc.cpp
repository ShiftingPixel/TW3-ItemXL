// Writes a DLC definition (.reddlc, a CR2W file) that mounts definition XMLs.
// The game's DLC manager finds a .reddlc in any bundle, also in a mod in Mods\.
//
//   CDLCDefinition                    id, mounters [handles], visibleInDLCMenu = false
//     CR4DefinitionsDLCMounter        definitionXmlFilePath  (a file, or a folder ending in \: every XML in it)
//     CR4DefinitionsNGPlusDLCMounter  definitionXmlFilePath  (the same for New Game Plus: the *_plus folders)
//
// CR2W layout: header 0x28 bytes (magic, version 164, flags, timestamp, build, file size, buffer size, crc, table
// count 6), 10 table headers (offset, count, crc32), tables: strings, names (string offset, FNV-1a 32 hash with the
// closing 0), imports, properties (one empty entry), exports (class name, flags, parent, size, offset, template,
// crc32 of the data). Header crc = crc32 of the first 0xA0 bytes with the crc field set to 0xDEADBEEF.
// The writer was checked byte for byte against a .reddlc made by the game's tools.
#include "itemxl.hpp"

#include <algorithm>

namespace itemxl {

namespace {

void put16(Bytes& b, uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
void put32(Bytes& b, uint32_t v) { for (int k = 0; k < 4; k++) b.push_back((uint8_t)(v >> (8 * k))); }
void set32(Bytes& b, size_t at, uint32_t v) { for (int k = 0; k < 4; k++) b[at + k] = (uint8_t)(v >> (8 * k)); }

uint32_t name_hash(const std::string& s) {
    if (s.empty()) return 0;
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) h = (h ^ c) * 0x01000193u;
    return (h ^ 0) * 0x01000193u;   // the closing 0 byte
}

Bytes packed_string(const std::string& s) {
    size_t n = s.size();
    Bytes out{(uint8_t)(0x80 | (n & 0x3F) | (n > 0x3F ? 0x40 : 0))};
    n >>= 6;
    while (n) {
        out.push_back((uint8_t)((n & 0x7F) | (n > 0x7F ? 0x80 : 0)));
        n >>= 7;
    }
    out.insert(out.end(), s.begin(), s.end());
    return out;
}

}  // namespace

// the path property of a mounter class (the folder of the item appearance templates, .w2ent, for
// CR4DefinitionsEntitieTemplatesDLCMounter: seen in the .reddlc of mods that bring templates); the others take an XML path
static std::string path_prop(const std::string& cls) {
    return cls == "CR4DefinitionsEntitieTemplatesDLCMounter" ? "entitieTemplatesDirectoryPath" : "definitionXmlFilePath";
}

Bytes build_reddlc(const std::string& dlc_id, const std::vector<std::pair<std::string, std::string>>& mounters) {
    const std::string ARRAY = "array:2,0,ptr:IGameplayDLCMounter";
    std::vector<std::string> names;
    auto nm = [&](const std::string& s) -> uint16_t {
        auto it = std::find(names.begin(), names.end(), s);
        if (it != names.end()) return (uint16_t)(it - names.begin());
        names.push_back(s);
        return (uint16_t)(names.size() - 1);
    };
    // names in the order the game's writer meets them: root class and its first properties, the mounters, the rest
    for (const char* s : {"", "CDLCDefinition", "id", "CName"}) nm(s);
    nm(dlc_id);
    nm("mounters");
    nm(ARRAY);
    for (auto& m : mounters) { nm(m.first); nm(path_prop(m.first)); nm("String"); }
    nm("visibleInDLCMenu");
    nm("Bool");

    auto prop = [&](Bytes& out, const std::string& name, const std::string& type, const Bytes& value) {
        put16(out, nm(name));
        put16(out, nm(type));
        put32(out, (uint32_t)(4 + value.size()));
        out.insert(out.end(), value.begin(), value.end());
    };
    struct Chunk { std::string cls; uint16_t flags; uint32_t parent; Bytes data; };
    std::vector<Chunk> chunks;
    Bytes root{0};
    Bytes idv;
    put16(idv, nm(dlc_id));
    prop(root, "id", "CName", idv);
    Bytes arr;
    put32(arr, (uint32_t)mounters.size());
    for (size_t i = 0; i < mounters.size(); i++) put32(arr, (uint32_t)(i + 2));
    prop(root, "mounters", ARRAY, arr);
    prop(root, "visibleInDLCMenu", "Bool", Bytes{0});
    root.push_back(0);
    root.push_back(0);
    chunks.push_back({"CDLCDefinition", 8192, 0, root});
    for (auto& m : mounters) {
        Bytes c{0};
        prop(c, path_prop(m.first), "String", packed_string(m.second));
        c.push_back(0);
        c.push_back(0);
        chunks.push_back({m.first, 8200, 1, c});
    }

    Bytes strings, name_tab;
    for (auto& n : names) {
        put32(name_tab, (uint32_t)strings.size());
        put32(name_tab, name_hash(n));
        strings.insert(strings.end(), n.begin(), n.end());
        strings.push_back(0);
    }
    Bytes props(16, 0);
    uint32_t pos = 0xA0;
    uint32_t s_off = pos; pos += (uint32_t)strings.size();
    uint32_t n_off = pos; pos += (uint32_t)name_tab.size();
    uint32_t p_off = pos; pos += (uint32_t)props.size();
    uint32_t e_off = pos; pos += 24 * (uint32_t)chunks.size();
    uint32_t data_off = pos;
    Bytes exports, data;
    for (auto& c : chunks) {
        put16(exports, nm(c.cls));
        put16(exports, c.flags);
        put32(exports, c.parent);
        put32(exports, (uint32_t)c.data.size());
        put32(exports, data_off + (uint32_t)data.size());
        put32(exports, 0);
        put32(exports, crc32(c.data.data(), c.data.size()));
        data.insert(data.end(), c.data.begin(), c.data.end());
    }
    uint32_t size = data_off + (uint32_t)data.size();
    Bytes head(0xA0, 0);
    head[0] = 'C'; head[1] = 'R'; head[2] = '2'; head[3] = 'W';
    set32(head, 4, 164);
    set32(head, 24, size);
    set32(head, 28, size);
    set32(head, 32, 0xDEADBEEFu);
    set32(head, 36, 6);
    struct T { uint32_t off, count, crc; };
    T tables[6] = {{s_off, (uint32_t)strings.size(), crc32(strings.data(), strings.size())},
                   {n_off, (uint32_t)names.size(), crc32(name_tab.data(), name_tab.size())},
                   {0, 0, 0},
                   {p_off, 1, crc32(props.data(), props.size())},
                   {e_off, (uint32_t)chunks.size(), crc32(exports.data(), exports.size())},
                   {0, 0, 0}};
    for (int i = 0; i < 6; i++) {
        set32(head, 0x28 + i * 12, tables[i].off);
        set32(head, 0x2C + i * 12, tables[i].count);
        set32(head, 0x30 + i * 12, tables[i].crc);
    }
    set32(head, 32, crc32(head.data(), head.size()));
    Bytes out = head;
    for (const Bytes* b : {&strings, &name_tab, &props, &exports, &data}) out.insert(out.end(), b->begin(), b->end());
    return out;
}

Bytes itemxl_reddlc(bool templates) {
    std::vector<std::pair<std::string, std::string>> m{
        {"CR4DefinitionsDLCMounter", std::string(ITEMXL_DLC_ROOT) + "\\data\\gameplay\\items\\"},
        {"CR4DefinitionsNGPlusDLCMounter", std::string(ITEMXL_DLC_ROOT) + "\\data\\gameplay\\items_plus\\"}};
    if (templates)   // the copied templates of $appearance (appearance.cpp)
        m.push_back({"CR4DefinitionsEntitieTemplatesDLCMounter", std::string(ITEMXL_DLC_ROOT) + "\\data\\items\\"});
    return build_reddlc(ITEMXL_DLC_ID, m);
}

}  // namespace itemxl
