// Reading Witcher 3 .bundle files (POTATO70) and the two compressions the definition XMLs use.
//
// Bundle layout (little endian):
//   header 0x20: "POTATO70", u32 bundle size, u32 0, u32 table size (@0x10), ...
//   table: one entry per file. Two layouts occur:
//     0x130 bytes (game 4.x/5.x, W3Bundle): name[0x100] hash[0x10] u64 offset, u32 size, u32 zsize, u32 crc, u32 comp
//     0x140 bytes (classic wcc_lite):       name[0x100] hash[0x10] u32 ?, u32 size, u32 zsize, u32 offset, ...,
//                                           u32 comp at 0x13C
//   The layout whose entries all fit into the file is the right one.
//   compression: 0 none, 1 zlib, 4/5 lz4 block
#include "itemxl.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace itemxl {

// ------------------------------------------------------------------ zlib ----
// A plain inflate (RFC 1950/1951): stored, fixed and dynamic Huffman blocks.

namespace {

struct BitReader {
    const uint8_t* p;
    size_t n, pos = 0;
    uint32_t bitbuf = 0;
    int bitcnt = 0;
    bool fail = false;
    int bits(int need) {
        while (bitcnt < need) {
            if (pos >= n) { fail = true; return 0; }
            bitbuf |= (uint32_t)p[pos++] << bitcnt;
            bitcnt += 8;
        }
        int v = (int)(bitbuf & ((1u << need) - 1));
        bitbuf >>= need;
        bitcnt -= need;
        return v;
    }
};

struct Huffman {
    short count[16] = {0};
    short symbol[320] = {0};
};

int build(Huffman& h, const short* lengths, int n) {
    std::memset(h.count, 0, sizeof h.count);
    for (int s = 0; s < n; s++) h.count[lengths[s]]++;
    if (h.count[0] == n) return 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return -1;
    }
    short offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + h.count[len];
    for (int s = 0; s < n; s++)
        if (lengths[s]) h.symbol[offs[lengths[s]]++] = (short)s;
    return left;
}

int decode(BitReader& br, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= br.bits(1);
        if (br.fail) return -1;
        int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

const short LBASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                         35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const short LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short DBASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                         257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(BitReader& br, Bytes& out, const Huffman& lit, const Huffman& dist, size_t limit) {
    for (;;) {
        if (out.size() > limit) return false;
        int sym = decode(br, lit);
        if (sym < 0) return false;
        if (sym < 256) { out.push_back((uint8_t)sym); continue; }
        if (sym == 256) return true;
        sym -= 257;
        if (sym >= 29) return false;
        int len = LBASE[sym] + br.bits(LEXT[sym]);
        int ds = decode(br, dist);
        if (ds < 0 || ds >= 30) return false;
        size_t d = (size_t)DBASE[ds] + (size_t)br.bits(DEXT[ds]);
        if (br.fail || d > out.size()) return false;
        size_t start = out.size() - d;
        for (int k = 0; k < len; k++) out.push_back(out[start + k]);
    }
}

}  // namespace

bool zlib_inflate(const uint8_t* src, size_t n, Bytes& out, size_t limit) {
    if (n < 2) return false;
    BitReader br{src + 2, n - 2};     // skip the zlib header (CMF, FLG)
    int last;
    do {
        last = br.bits(1);
        int type = br.bits(2);
        if (br.fail) return false;
        if (type == 0) {                                // stored
            br.bitbuf = 0; br.bitcnt = 0;
            if (br.pos + 4 > br.n) return false;
            unsigned len = br.p[br.pos] | (br.p[br.pos + 1] << 8);
            br.pos += 4;
            if (br.pos + len > br.n || out.size() + len > limit) return false;
            out.insert(out.end(), br.p + br.pos, br.p + br.pos + len);
            br.pos += len;
        } else if (type == 1) {                         // fixed Huffman
            static Huffman lit, dist;
            static bool ready = false;
            if (!ready) {
                short l[288];
                int s = 0;
                for (; s < 144; s++) l[s] = 8;
                for (; s < 256; s++) l[s] = 9;
                for (; s < 280; s++) l[s] = 7;
                for (; s < 288; s++) l[s] = 8;
                build(lit, l, 288);
                for (s = 0; s < 30; s++) l[s] = 5;
                build(dist, l, 30);
                ready = true;
            }
            if (!codes(br, out, lit, dist, limit)) return false;
        } else if (type == 2) {                         // dynamic Huffman
            static const short ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            int nlen = br.bits(5) + 257, ndist = br.bits(5) + 1, ncode = br.bits(4) + 4;
            if (br.fail || nlen > 286 || ndist > 30) return false;
            short lengths[320] = {0};
            for (int i = 0; i < ncode; i++) lengths[ORDER[i]] = (short)br.bits(3);
            Huffman lencode, lit, dist;
            if (build(lencode, lengths, 19) != 0) return false;
            int index = 0;
            while (index < nlen + ndist) {
                int sym = decode(br, lencode);
                if (sym < 0) return false;
                if (sym < 16) { lengths[index++] = (short)sym; continue; }
                short len = 0;
                int rep;
                if (sym == 16) {
                    if (index == 0) return false;
                    len = lengths[index - 1];
                    rep = 3 + br.bits(2);
                } else if (sym == 17) rep = 3 + br.bits(3);
                else rep = 11 + br.bits(7);
                if (index + rep > nlen + ndist) return false;
                while (rep--) lengths[index++] = len;
            }
            if (lengths[256] == 0) return false;
            int err = build(lit, lengths, nlen);
            if (err < 0 || (err > 0 && nlen - lit.count[0] != 1)) return false;
            err = build(dist, lengths + nlen, ndist);
            if (err < 0 || (err > 0 && ndist - dist.count[0] != 1)) return false;
            if (!codes(br, out, lit, dist, limit)) return false;
        } else {
            return false;
        }
    } while (!last);
    return true;
}

// ------------------------------------------------------------------- lz4 ----

bool lz4_block_decompress(const uint8_t* src, size_t n, Bytes& out, size_t size) {
    out.clear();
    out.reserve(std::min(size, n * 255 + 64));   // (a block grows 255 times at most: no huge reserve for a false size)
    size_t i = 0;
    while (i < n) {
        uint8_t token = src[i++];
        size_t lit = token >> 4;
        if (lit == 15) {
            uint8_t b;
            do { if (i >= n) return false; b = src[i++]; lit += b; } while (b == 255);
        }
        if (i + lit > n || out.size() + lit > size) return false;
        out.insert(out.end(), src + i, src + i + lit);
        i += lit;
        if (i >= n) break;
        if (i + 2 > n) return false;
        size_t off = src[i] | (src[i + 1] << 8);
        i += 2;
        size_t ml = token & 15;
        if (ml == 15) {
            uint8_t b;
            do { if (i >= n) return false; b = src[i++]; ml += b; } while (b == 255);
        }
        ml += 4;
        if (off == 0 || off > out.size() || out.size() + ml > size) return false;
        size_t start = out.size() - off;
        for (size_t k = 0; k < ml; k++) out.push_back(out[start + k]);
    }
    return out.size() == size;
}

// ---------------------------------------------------------------- bundle ----

static uint32_t u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t u64(const uint8_t* p) { return u32(p) | ((uint64_t)u32(p + 4) << 32); }

std::vector<BundleEntry> bundle_entries(const std::string& path) {
    std::vector<BundleEntry> none;
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) return none;
    uint8_t head[0x20];
    if (!f.read((char*)head, 0x20) || std::memcmp(head, "POTATO70", 8) != 0) return none;
    uint32_t table_size = u32(head + 0x10);
    f.seekg(0, std::ios::end);
    uint64_t fsize = (uint64_t)f.tellg();
    if (table_size > fsize) return none;
    Bytes table(table_size);
    f.seekg(0x20);
    if (!f.read((char*)table.data(), table_size)) return none;

    for (int layout = 0; layout < 2; layout++) {
        uint32_t esize = layout == 0 ? 0x130 : 0x140;
        if (table_size == 0 || table_size % esize) continue;
        std::vector<BundleEntry> found;
        bool fits = true;
        for (uint32_t i = 0; i < table_size; i += esize) {
            const uint8_t* e = table.data() + i;
            BundleEntry be;
            be.bundle = path;
            be.name.assign((const char*)e, strnlen((const char*)e, 0x100));
            if (be.name.empty()) continue;
            if (layout == 0) {
                be.offset = u64(e + 0x110);
                be.size = u32(e + 0x118);
                be.zsize = u32(e + 0x11C);
                be.compression = u32(e + 0x124);
            } else {
                be.size = u32(e + 0x114);
                be.zsize = u32(e + 0x118);
                be.offset = u32(e + 0x11C);
                be.compression = u32(e + 0x13C);
            }
            if (be.compression > 5 || be.offset == 0 || be.offset + be.zsize > fsize) { fits = false; break; }
            found.push_back(std::move(be));
        }
        if (fits && !found.empty()) return found;
    }
    return none;
}

bool bundle_read(const BundleEntry& e, Bytes& out) {
    std::ifstream f(std::filesystem::u8path(e.bundle), std::ios::binary);
    if (!f) return false;
    Bytes raw(e.zsize);
    f.seekg((std::streamoff)e.offset);
    if (e.zsize && !f.read((char*)raw.data(), e.zsize)) return false;
    switch (e.compression) {
        case 0: out = std::move(raw); return true;
        // (a file may not grow past twice its stated size, 64 MB at least: no memory bomb from a made-up bundle)
        case 1:
            out.clear();
            return zlib_inflate(raw.data(), raw.size(), out, std::max<size_t>((size_t)e.size * 2, (size_t)64 << 20));
        case 4:
        case 5: return lz4_block_decompress(raw.data(), raw.size(), out, e.size);
        default: return false;
    }
}

// gameplay\...\*.xml of the base game, and dlc\<name>\data\...\*.xml of a DLC (Hearts of Stone, Blood and
// Wine, the free DLCs and the DLC folders of mods).
bool is_definition_xml(const std::string& name) {
    std::string n = lower(name);
    if (!ends_with(n, ".xml")) return false;
    if (starts_with(n, "gameplay\\")) return true;
    if (!starts_with(n, "dlc\\")) return false;
    size_t s = n.find('\\', 4);
    return s != std::string::npos && n.compare(s, 6, "\\data\\") == 0;
}

// A DLC's .reddlc may mount any folder below data\ (some mods use data\xml\items\), so outside data\gameplay\ the
// text decides: a definition file has a <definitions> element.
bool is_definition_data(const std::string& name, const Bytes& data) {
    std::string n = lower(name);
    if (starts_with(n, "gameplay\\")) return true;
    size_t s = n.find('\\', 4);
    if (s != std::string::npos && n.compare(s, 15, "\\data\\gameplay\\") == 0) return true;
    static const std::string tag = "<definitions";
    return std::search(data.begin(), data.end(), tag.begin(), tag.end()) != data.end();
}

}  // namespace itemxl
