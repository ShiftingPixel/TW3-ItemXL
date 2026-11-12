// Writing a mod's content folder: blob0.bundle and metadata.store, as wcc_lite / W3Bundle write them.
// The metadata.store part was matched byte for byte against W3Bundle's output.
//
// blob0.bundle
//   header 0x20: "POTATO70", u32 bundleSize (data end, aligned 16), u32 0, u32 tableSize,
//                u16 5, u16 dataOffset (0x20 + tableSize), 8 zero bytes
//   entry 0x130: name[0x100], hash[0x10] = 0, u64 offset, u32 size, u32 zsize, u32 crc32(data), u32 compression, 8 zero
//   data: each file at a 16-aligned offset, stored uncompressed (compression 0; the game reads that too)
//
// metadata.store (VLQ = CR2W packed int: 6 bits, then 7 bits per byte)
//   "\x03VTM", u32 7, u32 max zsize, u32 max size
//   VLQ n, string table: "", bundle name, file paths, "" (root), directory names
//   VLQ count, file infos  (32 bytes): nameOff, 0, zsize, size, entryIndex, 0, 0, compression   (index 0 empty)
//   VLQ count, file entries (24 bytes): u64 offset, u64 zsize, u32 fileIndex, u32 bundleIndex    (index 0 empty)
//   VLQ count, bundles     (24 bytes): u64 dataSize, u32 dataOffset, u32 nameOff, u32 firstFile, u32 numFiles
//   0x80, VLQ count, directories (8 bytes): u32 nameOff, u32 parentIndex
//   VLQ count, file->dir   (12 bytes): u32 fileIndex, u32 dirIndex, u32 nameOff of the file name
//   VLQ count, hashes      (16 bytes, sorted): u64 fnv1a64(lower path), u32 fileIndex, i32 (0 for file 1, else -1)
#include "itemxl.hpp"

#include <algorithm>
#include <cstdio>

namespace itemxl {

namespace {

uint32_t align16(uint64_t n) { return (uint32_t)((n + 15) & ~(uint64_t)15); }

void put16(Bytes& b, uint16_t v) { b.push_back(v & 0xFF); b.push_back(v >> 8); }
void put32(Bytes& b, uint32_t v) { for (int k = 0; k < 4; k++) b.push_back((uint8_t)(v >> (8 * k))); }
void put64(Bytes& b, uint64_t v) { for (int k = 0; k < 8; k++) b.push_back((uint8_t)(v >> (8 * k))); }

void vlq(Bytes& b, uint64_t n) {
    uint8_t x = n & 0x3F;
    n >>= 6;
    if (n) x |= 0x40;
    b.push_back(x);
    while (n) {
        x = n & 0x7F;
        n >>= 7;
        if (n) x |= 0x80;
        b.push_back(x);
    }
}

struct Dir {
    std::map<std::string, Dir> dirs;            // lower-case name -> folder (std::map: sorted)
    std::vector<std::string> files;
};

}  // namespace

// W3Bundle's order: a folder's files (by name) before its subfolders (by name), depth first.
std::vector<std::string> walk_order(const std::vector<std::string>& paths) {
    Dir root;
    for (auto& p : paths) {
        auto parts = split(p, '\\');
        Dir* d = &root;
        for (size_t k = 0; k + 1 < parts.size(); k++) d = &d->dirs[lower(parts[k])];
        d->files.push_back(p);
    }
    std::vector<std::string> out;
    std::function<void(Dir&)> visit = [&](Dir& d) {
        auto base = [](const std::string& s) { return lower(s.substr(s.rfind('\\') + 1)); };
        std::stable_sort(d.files.begin(), d.files.end(), [&](auto& a, auto& b) { return base(a) < base(b); });
        out.insert(out.end(), d.files.begin(), d.files.end());
        for (auto& kv : d.dirs) visit(kv.second);
    };
    visit(root);
    return out;
}

// One bundle of a content folder: its name, entries (offsets in that bundle), size and data offset.
struct PackedBundle {
    std::string name;
    std::vector<BundleEntry> entries;
    uint32_t size = 0, data_offset = 0;
    bool buffers = false;          // buffers0.bundle: the .buffer files (a model's geometry)
};

static Bytes metadata_for(const std::vector<PackedBundle>& bundles) {
    Bytes strtab{0};
    auto add = [&](const std::string& s) {
        uint32_t off = (uint32_t)strtab.size();
        strtab.insert(strtab.end(), s.begin(), s.end());
        strtab.push_back(0);
        return off;
    };
    // files in bundle order; file index = position + 1
    std::vector<const BundleEntry*> files;
    std::vector<uint32_t> f_off, b_off, f_bundle;
    for (size_t bi = 0; bi < bundles.size(); bi++) {
        b_off.push_back(add(bundles[bi].name));
        for (auto& e : bundles[bi].entries) { files.push_back(&e); f_off.push_back(add(e.name)); f_bundle.push_back((uint32_t)bi + 1); }
    }
    auto in_buffers = [&](size_t i) { return bundles[f_bundle[i] - 1].buffers; };
    // the buffer files, and which file each one belongs to (<file>.<n>.buffer)
    std::vector<uint32_t> buffer_table;                                   // file indices of the buffer files
    std::map<std::string, std::pair<uint32_t, uint32_t>> owner_buffers;   // lower owner path -> (first, count)
    for (size_t i = 0; i < files.size(); i++) {
        if (!in_buffers(i)) continue;
        std::string n = lower(files[i]->name);
        n = n.substr(0, n.size() - 7);                    // drop ".buffer"
        std::string owner = n.substr(0, n.rfind('.'));    // drop ".<n>"
        auto& ob = owner_buffers[owner];
        if (!ob.second) ob.first = (uint32_t)buffer_table.size();
        ob.second++;
        buffer_table.push_back((uint32_t)i + 1);
    }

    // directories in the order the files meet them (buffer files are not in the folder tables)
    std::vector<std::pair<std::string, uint32_t>> dirs{{"", 0}};   // (name, parent)
    std::map<std::string, uint32_t> dir_index{{"", 0}};
    std::vector<uint32_t> file_dir(files.size(), 0);
    for (size_t i = 0; i < files.size(); i++) {
        if (in_buffers(i)) continue;
        auto parts = split(files[i]->name, '\\');
        parts.pop_back();
        std::string key;
        for (size_t k = 0; k < parts.size(); k++) {
            std::string parent = key;
            key += (k ? "\\" : "") + lower(parts[k]);
            if (!dir_index.count(key)) {
                dir_index[key] = (uint32_t)dirs.size();
                dirs.emplace_back(parts[k], dir_index[parent]);
            }
        }
        file_dir[i] = dir_index[key];
    }
    std::vector<uint32_t> d_off;
    for (auto& d : dirs) d_off.push_back(add(d.first));

    uint32_t max_z = 0, max_s = 0;
    for (auto* e : files) { max_z = std::max(max_z, e->zsize); max_s = std::max(max_s, e->size); }

    Bytes out{0x03, 'V', 'T', 'M'};
    put32(out, 7);
    put32(out, max_z);
    put32(out, max_s);
    vlq(out, strtab.size());
    out.insert(out.end(), strtab.begin(), strtab.end());

    vlq(out, files.size() + 1);
    out.insert(out.end(), 32, 0);
    for (size_t i = 0; i < files.size(); i++) {
        auto ob = owner_buffers.find(lower(files[i]->name));
        uint32_t first = ob == owner_buffers.end() ? 0 : ob->second.first;
        uint32_t count = ob == owner_buffers.end() ? 0 : ob->second.second;
        put32(out, f_off[i]); put32(out, 0); put32(out, files[i]->zsize); put32(out, files[i]->size);
        put32(out, (uint32_t)i + 1); put32(out, first); put32(out, count); put32(out, files[i]->compression);
    }

    vlq(out, files.size() + 1);
    out.insert(out.end(), 24, 0);
    for (size_t i = 0; i < files.size(); i++) {
        put64(out, files[i]->offset); put64(out, files[i]->zsize); put32(out, (uint32_t)i + 1); put32(out, f_bundle[i]);
    }

    vlq(out, bundles.size() + 1);
    out.insert(out.end(), 24, 0);
    uint32_t first_file = 1;
    for (size_t bi = 0; bi < bundles.size(); bi++) {
        const PackedBundle& pb = bundles[bi];
        put64(out, pb.buffers ? 0 : (uint64_t)pb.size - pb.data_offset);     // (W3Bundle writes 0 for buffers0)
        put32(out, pb.data_offset); put32(out, b_off[bi]); put32(out, first_file); put32(out, (uint32_t)pb.entries.size());
        first_file += (uint32_t)pb.entries.size();
    }

    if (buffer_table.empty()) out.push_back(0x80);           // (an empty table, as W3Bundle writes it)
    else {
        vlq(out, buffer_table.size());
        for (auto f : buffer_table) put32(out, f);
    }
    vlq(out, dirs.size());
    for (size_t i = 0; i < dirs.size(); i++) { put32(out, d_off[i]); put32(out, dirs[i].second); }

    std::vector<size_t> listed;
    for (size_t i = 0; i < files.size(); i++)
        if (!in_buffers(i)) listed.push_back(i);
    vlq(out, listed.size());
    for (size_t i : listed) {
        size_t slash = files[i]->name.rfind('\\');
        uint32_t base = slash == std::string::npos ? 0 : (uint32_t)slash + 1;
        put32(out, (uint32_t)i + 1); put32(out, file_dir[i]); put32(out, f_off[i] + base);
    }

    std::vector<std::pair<uint64_t, uint32_t>> hashes;
    for (size_t i : listed) hashes.emplace_back(fnv1a64(lower(files[i]->name)), (uint32_t)i + 1);
    std::sort(hashes.begin(), hashes.end());
    vlq(out, hashes.size());
    for (auto& h : hashes) {
        put64(out, h.first);
        put32(out, h.second);
        // W3Bundle: 0 for file 1 and -1 for the others; with a buffers bundle 0 for all
        put32(out, (h.second == 1 || !buffer_table.empty()) ? 0u : 0xFFFFFFFFu);
    }
    return out;
}

// One bundle file from files (depot path -> bytes), in the order W3Bundle uses.
static Bytes bundle_bytes(const std::map<std::string, Bytes>& files, PackedBundle& pb) {
    std::vector<std::string> paths;
    for (auto& kv : files) paths.push_back(kv.first);
    auto order = walk_order(paths);
    uint32_t table = 0x130 * (uint32_t)order.size();
    uint32_t data_off = 0x20 + table;
    Bytes body;
    uint32_t pos = data_off;
    for (auto& name : order) {
        const Bytes& data = files.at(name);
        body.resize(pos - data_off, 0);
        BundleEntry e;
        e.name = name;
        e.offset = pos;
        e.size = e.zsize = (uint32_t)data.size();
        e.compression = 0;
        pb.entries.push_back(e);
        body.insert(body.end(), data.begin(), data.end());
        pos = align16((uint64_t)pos + data.size());
    }
    pb.size = align16((uint64_t)data_off + body.size());
    pb.data_offset = data_off;
    Bytes out{'P', 'O', 'T', 'A', 'T', 'O', '7', '0'};
    put32(out, pb.size); put32(out, 0); put32(out, table); put16(out, 5); put32(out, data_off);     // u32, as in the game's bundles: more than 214 files pass 0xFFFF
    out.insert(out.end(), 6, 0);
    for (auto& e : pb.entries) {
        Bytes name(0x110, 0);
        std::copy(e.name.begin(), e.name.begin() + std::min<size_t>(e.name.size(), 0xFF), name.begin());   // never past the name field
        out.insert(out.end(), name.begin(), name.end());
        const Bytes& data = files.at(e.name);
        put64(out, e.offset); put32(out, e.size); put32(out, e.zsize);
        put32(out, crc32(data.data(), data.size())); put32(out, 0);
        out.insert(out.end(), 8, 0);
    }
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

// blob0.bundle (and buffers0.bundle for the .buffer files: the game reads a model's geometry only from there)
// and metadata.store, as W3Bundle writes them.
bool write_content(const std::string& content_dir, const std::map<std::string, Bytes>& files, std::string& error) {
    std::map<std::string, Bytes> main_files, buffer_files;
    for (auto& kv : files) (ends_with(lower(kv.first), ".buffer") ? buffer_files : main_files)[kv.first] = kv.second;
    std::vector<PackedBundle> bundles(1);
    bundles[0].name = "blob0.bundle";
    Bytes blob = bundle_bytes(main_files, bundles[0]);
    Bytes buf;
    if (!buffer_files.empty()) {
        bundles.emplace_back();
        bundles[1].name = "buffers0.bundle";
        bundles[1].buffers = true;
        buf = bundle_bytes(buffer_files, bundles[1]);
    }
    make_dirs(content_dir);
    Bytes meta = metadata_for(bundles);
    bool ok = write_file(content_dir + "\\blob0.bundle", blob.data(), blob.size()) &&
              write_file(content_dir + "\\metadata.store", meta.data(), meta.size());
    if (ok && !buf.empty()) ok = write_file(content_dir + "\\buffers0.bundle", buf.data(), buf.size());
    if (ok && buf.empty()) std::remove((content_dir + "\\buffers0.bundle").c_str());
    if (!ok) error = "cannot write " + content_dir;
    return ok;
}

}  // namespace itemxl
