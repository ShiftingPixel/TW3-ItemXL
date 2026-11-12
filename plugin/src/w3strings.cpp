// Reads the game's text files (<content>\<lang>.w3strings). ItemXL takes words
// the game uses itself from them (how a diagram is named in every language) instead of guessing.
//
// Format (as documented by the w3strings encoder / w3strings-rust; this is an own implementation):
//   "RTSW", u32 version (162; Remastered 164), u16 key1 ... u16 key2 as the file's last two bytes.
//   key1 << 16 | key2 names the language, which gives a 32-bit magic.
//   From offset 10:
//     VLQ n, n x (u32 id ^ magic, u32 offset, u32 length)       texts
//     VLQ n, n x (u32 key hash, u32 id ^ magic)                  keys -> ids
//     VLQ n (size of the text area), then the text area
//   A text is XOR'd unit by unit with ((length + 1) * k), k starting at (magic >> 8) & 0xFFFF and rotated left by 1
//   after each unit. Version 162: UTF-16 units, offsets/lengths in units. Version 164: UTF-8 bytes, offsets/lengths
//   in bytes, the low byte of the key per byte.
//   A key's hash: h = h * 31 + c over the UTF-16 units of the lower-case key (32 bit).
#include "itemxl.hpp"

#include <algorithm>
#include <cstring>
#include <map>

namespace itemxl {

namespace {

uint32_t rd32(const Bytes& d, size_t p) { return d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | ((uint32_t)d[p + 3] << 24); }
uint16_t rd16(const Bytes& d, size_t p) { return (uint16_t)(d[p] | (d[p + 1] << 8)); }

bool vlq(const Bytes& d, size_t& p, uint32_t& v) {
    if (p >= d.size()) return false;
    uint8_t b = d[p++];
    v = b & 0x3F;
    int shift = 6;
    bool more = b & 0x40;
    while (more) {
        if (p >= d.size()) return false;
        b = d[p++];
        v |= (uint32_t)(b & 0x7F) << shift;
        shift += 7;
        more = b & 0x80;
    }
    return true;
}

uint32_t magic_of(uint32_t key, bool& known) {
    static const std::map<uint32_t, uint32_t> M = {
        {0x83496237u, 0x73946816u}, {0x43975139u, 0x79321793u}, {0x75886138u, 0x42791159u},
        {0x45931894u, 0x12375973u}, {0x23863176u, 0x75921975u}, {0x24987354u, 0x21793217u},
        {0x18796651u, 0x42387566u}, {0x18632176u, 0x16875467u}, {0x63481486u, 0x42386347u},
        {0x42378932u, 0x67823218u}, {0x54834893u, 0x59825646u}, {0u, 0u}};
    auto it = M.find(key);
    known = it != M.end();
    return known ? it->second : 0;
}

void put_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
    }
}

bool valid_utf8(const std::string& s) {
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + n >= s.size() + (n ? 0 : 1)) return false;
        for (int k = 1; k <= n; k++)
            if (i + k >= s.size() || ((unsigned char)s[i + k] >> 6) != 2) return false;
        i += n + 1;
    }
    return true;
}

}  // namespace

uint32_t w3strings_key_hash(const std::string& key_utf8) {
    // lower case (ASCII is enough for the game's keys), then over UTF-16 units: keys are ASCII
    uint32_t h = 0;
    for (unsigned char c : lower(key_utf8)) h = h * 31 + c;
    return h;
}

bool W3Strings::load(const std::string& path) {
    if (!read_file(path, data) || data.size() < 16 || std::memcmp(data.data(), "RTSW", 4) != 0) return false;
    version = rd32(data, 4);
    uint32_t key = (uint32_t)rd16(data, 8) << 16 | rd16(data, data.size() - 2);
    bool known;
    magic = magic_of(key, known);
    if (!known) return false;
    size_t p = 10;
    uint32_t n;
    if (!vlq(data, p, n) || p + (size_t)n * 12 > data.size()) return false;
    for (uint32_t i = 0; i < n; i++, p += 12) entries[rd32(data, p) ^ magic] = {rd32(data, p + 4), rd32(data, p + 8)};
    if (!vlq(data, p, n) || p + (size_t)n * 8 > data.size()) return false;
    for (uint32_t i = 0; i < n; i++, p += 8) ids_by_hash.emplace(rd32(data, p), rd32(data, p + 4) ^ magic);
    uint32_t size;
    if (!vlq(data, p, size)) return false;
    start = p;
    // the text block: `size` bytes (Remastered, UTF-8) or `size` UTF-16 units. Its length decides, not the version:
    // some tools write the Remastered layout under version 162
    bytes = version >= 164 || start + (size_t)size + 2 == data.size();
    return true;
}

bool W3Strings::text(uint32_t id, std::string& out) const {
    auto it = entries.find(id);
    if (it == entries.end()) return false;
    uint32_t off = it->second.first, len = it->second.second;
    uint16_t k = (uint16_t)((magic >> 8) & 0xFFFF);
    out.clear();
    if (bytes) {
        if (start + off + len > data.size()) return false;
        for (uint32_t i = 0; i < len; i++) {
            out += (char)(data[start + off + i] ^ (uint8_t)(((len + 1) * (uint32_t)k) & 0xFF));
            k = (uint16_t)((k << 1) | (k >> 15));
        }
        if (!valid_utf8(out)) {                          // a few game texts are not valid UTF-8: kept as Latin-1
            std::string latin;
            for (unsigned char c : out) put_utf8(latin, c);
            out = latin;
        }
        return true;
    }
    if (start + 2 * ((size_t)off + len) > data.size()) return false;   // 64-bit: off + len must not wrap
    std::vector<uint16_t> units;
    for (uint32_t i = 0; i < len; i++) {
        units.push_back((uint16_t)(rd16(data, start + 2 * ((size_t)off + i)) ^ (((len + 1) * (uint32_t)k) & 0xFFFF)));
        k = (uint16_t)((k << 1) | (k >> 15));
    }
    for (size_t i = 0; i < units.size(); i++) {
        uint32_t cp = units[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < units.size()) cp = 0x10000 + ((cp - 0xD800) << 10) + (units[++i] - 0xDC00);
        put_utf8(out, cp);
    }
    return true;
}

bool W3Strings::by_key(const std::string& key, std::string& out) const {
    auto it = ids_by_hash.find(w3strings_key_hash(key));
    return it != ids_by_hash.end() && text(it->second, out);
}

// 'Schema: Scherben-Schwert' -> 'Schema: '; up to the first ':' or U+FF1A and the spaces after it.
static bool prefix_of(const std::string& t, std::string& out) {
    size_t a = t.find(':'), b = t.find("\xEF\xBC\x9A");
    size_t cut = std::min(a, b);
    if (cut == std::string::npos) return false;
    size_t end = cut + (cut == b ? 3 : 1);
    for (;;) {
        if (end < t.size() && t[end] == ' ') end++;
        else if (t.compare(end, 2, "\xC2\xA0") == 0) end += 2;
        else break;
    }
    out = t.substr(0, end);
    return true;
}

std::string diagram_prefix(const W3Strings& s, const std::vector<std::string>& schematic_keys) {
    std::map<std::string, int> counts;
    for (auto& k : schematic_keys) {
        std::string t, p;
        if (s.by_key(k, t) && prefix_of(t, p)) counts[p]++;
    }
    std::string best;
    int n = 0;
    for (auto& kv : counts)                              // std::map: the smallest text wins a tie
        if (kv.second > n) { best = kv.first; n = kv.second; }
    return best;
}

}  // namespace itemxl
