// CR2W, the format of the game's resources (.w2ent, .w2mesh, .xbm ...): read, change import paths and properties,
// write. Layout (as in the CR2W code of the TW3SE SDK, tools\cr2w\cr2w.py, which this follows):
//   header 0x28: "CR2W", u32 version, u32 flags, u64 timestamp, u32 build, u32 fileSize (end of the object data),
//                u32 bufferSize (whole file), u32 crc, u32 table count
//   10 table headers (offset, count, crc32 of the table's bytes): 0 strings, 1 names (string offset, FNV-1a 32 hash
//   with the closing 0), 2 imports (path offset, class name index u16, flags u16), 3 properties (16 bytes),
//   4 exports (class u16, flags u16, parent, size, offset, template, crc32 of the data), 5 buffers (flags, index,
//   offset, disk size, mem size, crc32); then the object data, then inline buffers.
//   The header crc is crc32 of the first 0xA0 bytes with the crc field set to 0xDEADBEEF.
// One difference to the SDK's writer: a buffer row with offset 0 points to an external file (<mesh>.1.buffer, in
// buffers0.bundle); it is kept as it is instead of being turned into inline data.
//
// An object's data: a 0 byte, then properties (u16 name, u16 type, u32 size counting itself, value), ended by
// name 0 (u16), then data of the class's own (a texture's mips ...).
#include "itemxl.hpp"

#include <cstring>

namespace itemxl {

namespace {

uint16_t rd16(const Bytes& b, size_t at) { return (uint16_t)(b[at] | b[at + 1] << 8); }
uint32_t rd32(const Bytes& b, size_t at) { return (uint32_t)b[at] | (uint32_t)b[at + 1] << 8 | (uint32_t)b[at + 2] << 16 | (uint32_t)b[at + 3] << 24; }
uint64_t rd64(const Bytes& b, size_t at) { return (uint64_t)rd32(b, at) | (uint64_t)rd32(b, at + 4) << 32; }
void put16(Bytes& b, uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
void put32(Bytes& b, uint32_t v) { for (int k = 0; k < 4; k++) b.push_back((uint8_t)(v >> (8 * k))); }
void put64(Bytes& b, uint64_t v) { put32(b, (uint32_t)v); put32(b, (uint32_t)(v >> 32)); }
void set32(Bytes& b, size_t at, uint32_t v) { for (int k = 0; k < 4; k++) b[at + k] = (uint8_t)(v >> (8 * k)); }

uint32_t cr2w_name_hash(const std::string& s) {
    if (s.empty()) return 0;
    uint32_t h = 0x811C9DC5u;
    for (unsigned char c : s) h = (h ^ c) * 0x01000193u;
    return h * 0x01000193u;   // the closing 0 byte
}

}  // namespace

// Every offset, count and index of the file is checked before it is used: a broken or hand-made resource of a mod
// gives an error, never a read outside the data (which would crash the game at start).
bool Cr2w::read(const Bytes& d, std::string& error) {
    if (d.size() < 0xA0 || std::memcmp(d.data(), "CR2W", 4) != 0) { error = "not a CR2W file"; return false; }
    version = rd32(d, 4);
    flags = rd32(d, 8);
    timestamp = rd64(d, 12);
    build = rd32(d, 20);
    table_count = rd32(d, 36);
    uint32_t off[10], cnt[10];
    for (int t = 0; t < 10; t++) { off[t] = rd32(d, 0x28 + 12 * t); cnt[t] = rd32(d, 0x28 + 12 * t + 4); }
    for (int t = 6; t < 10; t++)
        if (cnt[t]) { error = "a CR2W file with embedded files (not supported)"; return false; }
    // a table of cnt rows of row bytes at off must lie inside the file (64-bit math: no overflow)
    static const uint32_t ROW[6] = {1, 8, 8, 16, 24, 24};
    static const char* TABLE[6] = {"strings", "names", "imports", "properties", "exports", "buffers"};
    for (int t = 0; t < 6; t++)
        if (cnt[t] && (uint64_t)off[t] + (uint64_t)cnt[t] * ROW[t] > d.size()) {
            error = std::string("broken CR2W file: the ") + TABLE[t] + " table lies outside the file";
            return false;
        }
    strings.clear();
    if (cnt[0]) strings.assign(d.begin() + off[0], d.begin() + off[0] + cnt[0]);
    // a 0-terminated string of the string table; false when the offset or the end is outside the table
    auto text = [&](uint32_t o, std::string& out) {
        if (o >= strings.size()) return false;
        const uint8_t* a = strings.data() + o;
        const void* z = std::memchr(a, 0, strings.size() - o);
        if (!z) return false;
        out.assign((const char*)a, (const uint8_t*)z - a);
        return true;
    };
    auto broken = [&](const std::string& what) { error = "broken CR2W file: " + what; return false; };
    names.clear();
    hashes.clear();
    for (uint32_t i = 0; i < cnt[1]; i++) {
        std::string n;
        if (!text(rd32(d, off[1] + 8 * i), n)) return broken("name " + std::to_string(i) + " is outside the string table");
        names.push_back(std::move(n));
        hashes.push_back(rd32(d, off[1] + 8 * i + 4));
    }
    imports.clear();
    for (uint32_t i = 0; i < cnt[2]; i++) {
        size_t e = off[2] + 8 * (size_t)i;
        std::string path;
        uint16_t cls = rd16(d, e + 4);
        if (!text(rd32(d, e), path)) return broken("import " + std::to_string(i) + " is outside the string table");
        if (cls >= names.size()) return broken("import " + std::to_string(i) + " names an unknown class");
        imports.push_back({path, names[cls], rd16(d, e + 6)});
    }
    properties.clear();
    for (uint32_t i = 0; i < cnt[3]; i++) properties.emplace_back(d.begin() + off[3] + 16 * i, d.begin() + off[3] + 16 * (i + 1));
    exports.clear();
    for (uint32_t i = 0; i < cnt[4]; i++) {
        size_t e = off[4] + 24 * (size_t)i;
        uint32_t size = rd32(d, e + 8), o = rd32(d, e + 12);
        uint16_t cls = rd16(d, e);
        if ((uint64_t)o + size > d.size()) { error = "export outside the file"; return false; }
        if (cls >= names.size()) return broken("export " + std::to_string(i) + " names an unknown class");
        exports.push_back({names[cls], rd16(d, e + 2), rd32(d, e + 4), rd32(d, e + 16), Bytes(d.begin() + o, d.begin() + o + size)});
    }
    buffers.clear();
    for (uint32_t i = 0; i < cnt[5]; i++) {
        size_t e = off[5] + 24 * (size_t)i;
        uint32_t at = rd32(d, e + 8);
        Buffer b{rd32(d, e), rd32(d, e + 4), rd32(d, e + 12), rd32(d, e + 16), rd32(d, e + 20), {}, at == 0};
        if (!b.external) {
            if ((uint64_t)at + b.disk > d.size()) return broken("buffer " + std::to_string(i) + " lies outside the file");
            b.data.assign(d.begin() + at, d.begin() + at + b.disk);
        }
        buffers.push_back(std::move(b));
    }
    return true;
}

uint16_t Cr2w::name(const std::string& n) {
    for (size_t i = 0; i < names.size(); i++)
        if (names[i] == n) return (uint16_t)i;
    names.push_back(n);
    hashes.push_back(cr2w_name_hash(n));
    return (uint16_t)(names.size() - 1);
}

Bytes Cr2w::write() {
    for (auto& e : exports) name(e.cls);
    for (auto& i : imports) name(i.cls);
    // strings: the file's own table as it was, new names and import paths after it
    Bytes str = strings;
    std::map<std::string, uint32_t> offs;
    for (size_t o = 0; o < strings.size();) {
        // (a last string without its closing 0 ends at the end of the table)
        const void* z = std::memchr(strings.data() + o, 0, strings.size() - o);
        size_t len = z ? (size_t)((const uint8_t*)z - (strings.data() + o)) : strings.size() - o;
        std::string s((const char*)strings.data() + o, len);
        offs.emplace(s, (uint32_t)o);
        o += len + 1;
    }
    auto put = [&](const std::string& s) {
        auto it = offs.find(s);
        if (it != offs.end()) return it->second;
        uint32_t o = (uint32_t)str.size();
        str.insert(str.end(), s.begin(), s.end());
        str.push_back(0);
        offs[s] = o;
        return o;
    };
    for (auto& n : names) put(n);
    for (auto& i : imports) put(i.path);
    Bytes nametab, imptab, proptab;
    for (size_t i = 0; i < names.size(); i++) { put32(nametab, offs[names[i]]); put32(nametab, hashes[i]); }
    for (auto& i : imports) { put32(imptab, offs[i.path]); put16(imptab, name(i.cls)); put16(imptab, i.flags); }
    for (auto& p : properties) proptab.insert(proptab.end(), p.begin(), p.end());

    struct Table { uint32_t off, cnt, crc; Bytes* blob; };
    std::vector<Table> layout;
    uint32_t pos = 0xA0;
    for (auto pr : {std::make_pair(&str, (uint32_t)str.size()), std::make_pair(&nametab, (uint32_t)names.size()),
                    std::make_pair(&imptab, (uint32_t)imports.size()), std::make_pair(&proptab, (uint32_t)properties.size())}) {
        layout.push_back({pos, pr.second, crc32(pr.first->data(), pr.first->size()), pr.first});
        pos += (uint32_t)pr.first->size();
    }
    uint32_t exports_off = pos;
    pos += 24 * (uint32_t)exports.size();
    uint32_t buffers_off = pos;
    pos += 24 * (uint32_t)buffers.size();
    uint32_t data_off = pos;
    Bytes exptab, datas;
    for (auto& e : exports) {
        put16(exptab, name(e.cls)); put16(exptab, e.flags); put32(exptab, e.parent); put32(exptab, (uint32_t)e.data.size());
        put32(exptab, data_off + (uint32_t)datas.size()); put32(exptab, e.tmpl); put32(exptab, crc32(e.data.data(), e.data.size()));
        datas.insert(datas.end(), e.data.begin(), e.data.end());
    }
    uint32_t file_size = data_off + (uint32_t)datas.size();
    Bytes buftab, bufdata;
    for (auto& b : buffers) {
        put32(buftab, b.flags); put32(buftab, b.index);
        if (b.external) {                                    // kept: the data is in <file>.<index>.buffer
            put32(buftab, 0); put32(buftab, b.disk); put32(buftab, b.mem); put32(buftab, b.crc);
        } else {
            put32(buftab, file_size + (uint32_t)bufdata.size()); put32(buftab, (uint32_t)b.data.size()); put32(buftab, b.mem);
            put32(buftab, crc32(b.data.data(), b.data.size()));
            bufdata.insert(bufdata.end(), b.data.begin(), b.data.end());
        }
    }
    layout.push_back({exports_off, (uint32_t)exports.size(), crc32(exptab.data(), exptab.size()), &exptab});
    layout.push_back({buffers_off, (uint32_t)buffers.size(), crc32(buftab.data(), buftab.size()), &buftab});

    Bytes out{'C', 'R', '2', 'W'};
    put32(out, version); put32(out, flags); put64(out, timestamp); put32(out, build);
    put32(out, file_size); put32(out, file_size + (uint32_t)bufdata.size()); put32(out, 0xDEADBEEF); put32(out, table_count);
    for (size_t t = 0; t < 10; t++) {
        if (t < layout.size()) { put32(out, layout[t].cnt ? layout[t].off : 0); put32(out, layout[t].cnt); put32(out, layout[t].crc); }
        else { put32(out, 0); put32(out, 0); put32(out, 0); }
    }
    set32(out, 32, crc32(out.data(), 0xA0));
    for (auto& t : layout) out.insert(out.end(), t.blob->begin(), t.blob->end());
    out.insert(out.end(), datas.begin(), datas.end());
    out.insert(out.end(), bufdata.begin(), bufdata.end());
    return out;
}

// ------------------------------------------------------------ properties ----

// The value of a top-level property of an object: (offset of the value in data, its size), or {0, 0}.
std::pair<size_t, size_t> Cr2w::find_prop(const Export& e, const std::string& prop) const {
    const Bytes& d = e.data;
    if (d.empty() || d[0] != 0) return {0, 0};
    size_t i = 1;
    while (i + 2 <= d.size()) {
        uint16_t n = rd16(d, i);
        if (n == 0 || i + 8 > d.size()) break;
        uint32_t size = rd32(d, i + 4);
        if (size < 4 || i + 4 + size > d.size()) break;
        if (n < names.size() && names[n] == prop) return {i + 8, size - 4};
        i += 4 + size;
    }
    return {0, 0};
}

bool Cr2w::set_prop(Export& e, const std::string& prop, const Bytes& value) {
    auto at = find_prop(e, prop);
    if (!at.first) return false;
    Bytes& d = e.data;
    d.erase(d.begin() + at.first, d.begin() + at.first + at.second);
    d.insert(d.begin() + at.first, value.begin(), value.end());
    set32(d, at.first - 4, (uint32_t)value.size() + 4);
    return true;
}

bool Cr2w::remove_prop(Export& e, const std::string& prop) {
    auto at = find_prop(e, prop);
    if (!at.first) return false;
    e.data.erase(e.data.begin() + at.first - 8, e.data.begin() + at.first + at.second);
    return true;
}

// A copy of a resource with its import paths changed (lower-case old path -> new path), in its flatCompiledData too.
// changed: how many imports were changed (in the file and its embedded copy).
// The game nests a flatCompiledData once; a deeper nesting is a broken (or hostile) file and stops at
// RETARGET_MAX_DEPTH instead of recursing without end.
static const int RETARGET_MAX_DEPTH = 8;

static Bytes retarget_at(const Bytes& data, const std::map<std::string, std::string>& paths, int& changed,
                         std::string& error, int depth) {
    if (depth > RETARGET_MAX_DEPTH) { error = "flatCompiledData nested too deep"; return {}; }
    Cr2w f;
    if (!f.read(data, error)) return {};
    for (auto& i : f.imports) {
        auto it = paths.find(lower(i.path));
        if (it != paths.end()) { i.path = it->second; changed++; }
    }
    for (auto& e : f.exports) {
        auto at = f.find_prop(e, "flatCompiledData");
        if (!at.first || at.second < 4) continue;
        Bytes inner(e.data.begin() + at.first + 4, e.data.begin() + at.first + at.second);
        Bytes re = retarget_at(inner, paths, changed, error, depth + 1);
        if (re.empty()) return {};
        Bytes value;
        put32(value, (uint32_t)re.size());
        value.insert(value.end(), re.begin(), re.end());
        f.set_prop(e, "flatCompiledData", value);
    }
    return f.write();
}

Bytes cr2w_retarget(const Bytes& data, const std::map<std::string, std::string>& paths, int& changed, std::string& error) {
    return retarget_at(data, paths, changed, error, 0);
}

// ---------------------------------------------------------------- .xbm ----

Bytes make_xbm(const TextureInfo& t, uint32_t cache_key, const Bytes& resident) {
    Cr2w f;
    f.version = 164;
    f.table_count = 6;
    f.name("");
    f.properties.push_back(Bytes(16, 0));
    Cr2w::Export e{"CBitmapTexture", 8192, 0, 0, {0}};
    auto prop = [&](const std::string& n, const std::string& type, const Bytes& v) {
        put16(e.data, f.name(n)); put16(e.data, f.name(type)); put32(e.data, (uint32_t)v.size() + 4);
        e.data.insert(e.data.end(), v.begin(), v.end());
    };
    Bytes v;
    f.name("CBitmapTexture");
    v.clear(); put32(v, t.width); prop("width", "Uint32", v);
    v.clear(); put32(v, t.height); prop("height", "Uint32", v);
    f.name("compression");
    f.name("ETextureCompression");
    v.clear(); put16(v, f.name(t.compression)); prop("compression", "ETextureCompression", v);
    f.name("textureGroup");
    f.name("CName");
    v.clear(); put16(v, f.name(t.group)); prop("textureGroup", "CName", v);
    v.clear(); v.push_back((uint8_t)t.resident_mip); prop("residentMipIndex", "Uint8", v);
    v.clear(); put32(v, cache_key); prop("textureCacheKey", "Uint32", v);
    put16(e.data, 0);
    // the texture's own data: 0, mip count, (width, height, pitch) per mip, resident size, 16, the resident mips
    put32(e.data, 0);
    put32(e.data, (uint32_t)t.mips.size());
    for (auto& m : t.mips) { put32(e.data, m.width); put32(e.data, m.height); put32(e.data, m.pitch); }
    put32(e.data, (uint32_t)resident.size());
    put32(e.data, 16);
    e.data.insert(e.data.end(), resident.begin(), resident.end());
    f.exports.push_back(std::move(e));
    return f.write();
}

}  // namespace itemxl
