// Textures of a mod: a DDS file becomes a .xbm resource and an entry in the texture.cache of ItemXL's DLC.
//
// DDS: "DDS ", header (height at 12, width at 16, mip count at 28, pixel format flags/fourCC/bits at 80..),
//   optional DX10 header (DXGI format), then all mips, the largest first.
// texture.cache (version 7, as the game's content0): the textures' data, each from a 4096-byte page; a table of
//   u32 piece offsets; the names (0-terminated); 52 bytes per entry; a 32-byte footer: FNV-1a 64 over table, names
//   and entries, pages used, entry count, names' size, table count, "HCXT", version.
//   An entry: key (= the .xbm's textureCacheKey), name offset, first page, stored size, size, 16, width, height,
//   mips, slices, first table index, pieces - 1 (+ 0x10000 in version 7), timestamp u64, format (7 DXT1, 8 DXT5,
//   10 BC7, 14 BC4, 15 BC5, 253 RGBA), 4, cube, 0. Its data: one piece per mip whose longer side is above 32, the
//   smaller mips together in the last piece; a piece is u32 stored size, u32 size, u8 pieces left, zlib.
//   (Layout from the texcache tool of the TW3SE SDK.)
// The .xbm keeps the small mips itself (from residentMipIndex on), the rest is read from the cache.
//
// Which compression and texture group a DDS gets (from 3000 textures of the game's characters, 10.11.2026):
//   DXT1            TCM_DXTNoAlpha    CharacterDiffuse            (diffuse _d, ambient _a, specular _s)
//   DXT5, name _n   TCM_NormalsGloss  CharacterNormalmapGloss     (normal map with gloss)
//   DXT5            TCM_DXTAlpha      CharacterDiffuseWithAlpha
//   BC7             TCM_QualityColor  QualityColor
//   BC5             TCM_Normals       CharacterNormal
//   BC4             TCM_QualityR      QualityR
#include "itemxl.hpp"

#include <cstring>

namespace itemxl {

namespace {

uint32_t rd32(const Bytes& b, size_t at) { return (uint32_t)b[at] | (uint32_t)b[at + 1] << 8 | (uint32_t)b[at + 2] << 16 | (uint32_t)b[at + 3] << 24; }
void put16(Bytes& b, uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
void put32(Bytes& b, uint32_t v) { for (int k = 0; k < 4; k++) b.push_back((uint8_t)(v >> (8 * k))); }
void put64(Bytes& b, uint64_t v) { put32(b, (uint32_t)v); put32(b, (uint32_t)(v >> 32)); }

int block_bytes(int fmt) {
    switch (fmt) { case 7: case 14: return 8; case 8: case 10: case 13: case 15: return 16; default: return 0; }
}

uint64_t mip_bytes(int fmt, uint32_t w, uint32_t h) {
    int bb = block_bytes(fmt);
    if (!bb) return (uint64_t)w * h * 4;
    return (uint64_t)std::max(1u, (w + 3) / 4) * std::max(1u, (h + 3) / 4) * (uint32_t)bb;
}

uint64_t fnv64(const Bytes& b) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (uint8_t c : b) h = (h ^ c) * 0x100000001b3ull;
    return h;
}

uint32_t adler32(const Bytes& d) {
    uint32_t a = 1, b = 0;
    for (uint8_t c : d) { a = (a + c) % 65521; b = (b + a) % 65521; }
    return b << 16 | a;
}

// zlib with stored blocks (no compression): any zlib reader takes it, the game's too
Bytes zstore(const Bytes& data) {
    Bytes out{0x78, 0x01};
    size_t n = data.size();
    for (size_t i = 0; i < std::max<size_t>(n, 1); i += 0xFFFF) {
        size_t len = std::min<size_t>(0xFFFF, n - std::min(i, n));
        out.push_back(i + 0xFFFF >= n ? 1 : 0);
        put16(out, (uint16_t)len);
        put16(out, (uint16_t)(0xFFFF ^ len));
        out.insert(out.end(), data.begin() + i, data.begin() + i + len);
    }
    uint32_t a = adler32(data);
    for (int k = 3; k >= 0; k--) out.push_back((uint8_t)(a >> (8 * k)));
    return out;
}

}  // namespace

bool read_dds(const Bytes& d, const std::string& name, TextureInfo& t, Bytes& pixels, std::string& error) {
    if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) { error = "not a DDS file"; return false; }
    t.height = rd32(d, 12);
    t.width = rd32(d, 16);
    uint32_t raw_mips = rd32(d, 28);
    if (t.width == 0 || t.height == 0 || t.width > 16384 || t.height > 16384 || raw_mips > 16) {
        error = "invalid DDS dimensions or mip count";
        return false;
    }
    uint32_t mips = std::max(1u, raw_mips);
    uint32_t pf_flags = rd32(d, 80), bits = rd32(d, 88);
    std::string fourcc((const char*)&d[84], 4);
    size_t start = 128;
    t.format = 0;
    if (fourcc == "DXT1") t.format = 7;
    else if (fourcc == "DXT5") t.format = 8;
    else if (fourcc == "ATI1" || fourcc == "BC4U") t.format = 14;
    else if (fourcc == "ATI2" || fourcc == "BC5U") t.format = 15;
    else if (fourcc == "DX10") {
        if (d.size() < 148) { error = "short DX10 header"; return false; }
        uint32_t dxgi = rd32(d, 128);
        start = 148;
        if (dxgi == 98 || dxgi == 99) t.format = 10;
        else if (dxgi == 71 || dxgi == 72) t.format = 7;
        else if (dxgi == 77 || dxgi == 78) t.format = 8;
        else if (dxgi == 80) t.format = 14;
        else if (dxgi == 83) t.format = 15;
    } else if (!(pf_flags & 4) && bits == 32) t.format = 253;
    if (!t.format) { error = "a DDS format the game does not use (" + fourcc + "); use DXT1, DXT5, BC4, BC5 or BC7"; return false; }
    bool normal = ends_with(lower(name.substr(0, name.rfind('.'))), "_n") ||
                  lower(name).find("_n0") != std::string::npos;
    switch (t.format) {
        case 7: t.compression = "TCM_DXTNoAlpha"; t.group = "CharacterDiffuse"; break;
        case 8: if (normal) { t.compression = "TCM_NormalsGloss"; t.group = "CharacterNormalmapGloss"; }
                else { t.compression = "TCM_DXTAlpha"; t.group = "CharacterDiffuseWithAlpha"; } break;
        case 10: t.compression = "TCM_QualityColor"; t.group = "QualityColor"; break;
        case 14: t.compression = "TCM_QualityR"; t.group = "QualityR"; break;
        case 15: t.compression = "TCM_Normals"; t.group = "CharacterNormal"; break;
        default: t.compression = "TCM_None"; t.group = "CharacterDiffuse"; break;
    }
    t.mips.clear();
    t.sizes.clear();
    uint64_t total = 0;
    t.resident_mip = -1;
    for (uint32_t m = 0; m < mips; m++) {
        uint32_t w = std::max(1u, t.width >> m), h = std::max(1u, t.height >> m);
        uint64_t s64 = mip_bytes(t.format, w, h);
        if (s64 > 0xFFFFFFFFull) { error = "mip too large"; return false; }
        uint32_t s = (uint32_t)s64;
        int bb = block_bytes(t.format);
        uint32_t tw = bb ? std::max(4u, w) : w, th = bb ? std::max(4u, h) : h;
        uint32_t pitch = bb ? std::max(1u, (w + 3) / 4) * (uint32_t)bb : w * 4;
        t.mips.push_back({tw, th, pitch});
        t.sizes.push_back(s);
        if (t.resident_mip < 0 && w <= 32 && h <= 32) t.resident_mip = (int)m;
        total += s;
    }
    if (t.resident_mip < 0) t.resident_mip = (int)mips - 1;
    if (d.size() < start || d.size() - start < total) { error = "the DDS is shorter than its mips"; return false; }
    pixels.assign(d.begin() + start, d.begin() + start + total);
    return true;
}

// The pieces of a texture: one per mip above 32 pixels, then the rest together (that last one is also the
// .xbm's resident data).
std::vector<Bytes> texture_pieces(const TextureInfo& t, const Bytes& pixels) {
    std::vector<Bytes> out;
    size_t at = 0;
    Bytes tail;
    for (size_t m = 0; m < t.sizes.size(); m++) {
        size_t len = std::min<size_t>(t.sizes[m], at <= pixels.size() ? pixels.size() - at : 0);
        Bytes part(pixels.begin() + at, pixels.begin() + at + len);
        at += len;
        if ((int)m < t.resident_mip) out.push_back(std::move(part));
        else tail.insert(tail.end(), part.begin(), part.end());
    }
    if (!tail.empty()) out.push_back(std::move(tail));
    return out;
}

uint32_t texture_key(const std::string& path) {
    uint32_t h = 0;
    for (unsigned char c : lower(path)) h = h * 31 + c;
    return h;
}

Bytes write_texture_cache(const std::vector<CacheTexture>& textures) {
    const uint32_t version = 7;   // as the game's (6 is read too)
    Bytes body, table, names, entries;
    std::vector<uint32_t> offsets_all;
    for (auto& tx : textures) {
        while (body.size() % 4096) body.push_back(0);
        uint32_t page = (uint32_t)(body.size() / 4096);
        size_t start = body.size();
        std::vector<uint32_t> offs;
        uint64_t size = 0;
        for (size_t k = 0; k < tx.pieces.size(); k++) {
            if (k) offs.push_back((uint32_t)(body.size() - start));
            Bytes z = zstore(tx.pieces[k]);
            put32(body, (uint32_t)z.size());
            put32(body, (uint32_t)tx.pieces[k].size());
            body.push_back((uint8_t)(tx.pieces.size() - 1 - k));
            body.insert(body.end(), z.begin(), z.end());
            size += tx.pieces[k].size();
        }
        uint32_t first = (uint32_t)offsets_all.size();
        for (auto o : offs) offsets_all.push_back(o);
        put32(entries, tx.key);
        put32(entries, (uint32_t)names.size());
        put32(entries, page);
        put32(entries, (uint32_t)(body.size() - start));
        put32(entries, (uint32_t)size);
        put32(entries, 16);
        put16(entries, (uint16_t)tx.info.width);
        put16(entries, (uint16_t)tx.info.height);
        put16(entries, (uint16_t)tx.info.mips.size());
        put16(entries, 1);
        put32(entries, offs.empty() ? 0 : first);   // one piece: 0, as in mods' icon caches
        put32(entries, (uint32_t)offs.size() + 0x10000);
        put64(entries, 399069065979692032ull);         // a timestamp; 0 is in none of the game's
        entries.push_back((uint8_t)tx.info.format);
        entries.push_back(4);
        entries.push_back(0);
        entries.push_back(0);
        names.insert(names.end(), tx.name.begin(), tx.name.end());
        names.push_back(0);
    }
    while (body.size() % 4096) body.push_back(0);
    for (auto o : offsets_all) put32(table, o);
    Bytes tail = table;
    tail.insert(tail.end(), names.begin(), names.end());
    tail.insert(tail.end(), entries.begin(), entries.end());
    Bytes out = body;
    out.insert(out.end(), tail.begin(), tail.end());
    put64(out, fnv64(tail));
    put32(out, (uint32_t)(body.size() / 4096));
    put32(out, (uint32_t)textures.size());
    put32(out, (uint32_t)names.size());
    put32(out, (uint32_t)offsets_all.size());
    out.insert(out.end(), {'H', 'C', 'X', 'T'});
    put32(out, version);
    return out;
}

}  // namespace itemxl
