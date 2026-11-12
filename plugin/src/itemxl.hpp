// ItemXL - record-level deltas for The Witcher 3 item and ability definitions.
//
// The C++ core. The parts:
//   bundle.cpp   reading .bundle files (zlib, lz4)
//   xml.cpp      a small XML DOM that writes the same text as Python's ElementTree
//   yaml.cpp     the YAML subset ItemXL files use (TweakXL style), with !append / !remove ... tags
//   model.cpp    records, applying deltas, turning whole-file overrides into deltas, the conflict ledger
//   pack.cpp     writing blob0.bundle and metadata.store
//   build.cpp    the whole build: read the game, read the mods, merge, write mod00000_ItemXL
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace itemxl {

// ------------------------------------------------------------------ util ----

std::string lower(std::string s);
std::string trim(const std::string& s);
bool starts_with(const std::string& s, const std::string& p);
bool ends_with(const std::string& s, const std::string& p);
std::vector<std::string> split(const std::string& s, char sep);
uint64_t fnv1a64(const std::string& s);
uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc = 0);
std::string fmt_double(double v);  // Python: f"{v:.6f}".rstrip("0").rstrip(".")

using Bytes = std::vector<uint8_t>;
bool read_file(const std::string& path, Bytes& out);
bool write_file(const std::string& path, const void* data, size_t n);
void make_dirs(const std::string& dir);

// ----------------------------------------------------------- compression ----

// zlib stream (header + deflate); false when the output would pass `limit` bytes
bool zlib_inflate(const uint8_t* src, size_t n, Bytes& out, size_t limit = (size_t)-1);
bool lz4_block_decompress(const uint8_t* src, size_t n, Bytes& out, size_t size);

// ---------------------------------------------------------------- bundle ----

struct BundleEntry {
    std::string bundle;  // file path of the bundle
    std::string name;    // depot path, backslashes
    uint64_t offset = 0;
    uint32_t size = 0, zsize = 0, compression = 0;
};

std::vector<BundleEntry> bundle_entries(const std::string& path);
bool bundle_read(const BundleEntry& e, Bytes& out);
bool is_definition_xml(const std::string& depot_path);
bool is_definition_data(const std::string& depot_path, const Bytes& data);   // also checks the text below a DLC

// ------------------------------------------------------------------- xml ----

struct Node {
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::string text, tail;
    std::vector<std::unique_ptr<Node>> children;

    const std::string* attr(const std::string& k) const;
    void set_attr(const std::string& k, const std::string& v);
    bool remove_attr(const std::string& k);
    Node* find(const std::string& child_tag) const;  // first child with this tag
    int index_of(const Node* child) const;
    std::unique_ptr<Node> clone() const;
};

// Parses an XML document; drops comments and processing instructions (as ElementTree does).
// Repeated attributes in one tag: the first one stays, `dropped` counts the others.
std::unique_ptr<Node> xml_parse(const std::string& text, std::string& error, int* dropped = nullptr);
std::string xml_to_string(const Node& n);           // ElementTree.tostring(encoding="unicode")
void xml_indent(Node& root, const std::string& space = "\t");
std::string xml_canon(const Node& n);               // texts stripped, no tails: for equality
std::unique_ptr<Node> xml_stripped(const Node& n);  // a copy with texts stripped and no tails

// ------------------------------------------------------------------ yaml ----

struct Value;
using Map = std::vector<std::pair<std::string, Value>>;

struct Value {
    enum Type { Null, Bool, Int, Float, Str, List, Dict, Op } type = Null;
    bool b = false;
    long long i = 0;
    double f = 0;
    std::string s;                 // Str: the text; Op: the operation ("append", "remove", ...)
    std::vector<Value> list;
    Map map;
    std::shared_ptr<Value> op;     // Op: the tagged value

    static Value str(std::string v) { Value x; x.type = Str; x.s = std::move(v); return x; }
    bool is_null() const { return type == Null; }
    bool is_map() const { return type == Dict; }
    bool is_list() const { return type == List; }
    const Value* get(const std::string& k) const;
    Value* get(const std::string& k);
    bool has(const std::string& k) const { return get(k) != nullptr; }
    void set(const std::string& k, Value v);
    std::string text() const;      // Python str() of a scalar; fmt() of ItemXL
    bool operator==(const Value& o) const;
    bool operator!=(const Value& o) const { return !(*this == o); }
};

// Every document of a YAML file. On a syntax error: empty list and `error` set.
std::vector<Value> yaml_load_all(const std::string& text, std::string& error);
Value yaml_resolve_scalar(const std::string& plain);   // plain scalar -> null/bool/int/float/str (YAML 1.1)
std::string to_json(const Value& v);                     // for the tests

// ----------------------------------------------------------------- model ----

struct DefFile {
    std::string path;   // depot path as the game spells it
    std::unique_ptr<Node> root;
    bool dirty = false;
    int rank = 0;       // load rank: 0 base game (gameplay\), 1 DLC files of the game's content, 2 a DLC folder
    int order = 0;      // position in the load order
};

struct Rec {
    DefFile* file;
    Node* parent;
    Node* el;
};

struct Index {
    std::map<std::string, DefFile*> files;   // lower-case depot path -> file
    std::map<std::pair<std::string, std::string>, std::vector<Rec>> rec;   // copies in load order
    void build();
    std::vector<Rec>& get(const std::string& tag, const std::string& name) { return rec[{tag, name}]; }
    void sort(std::vector<Rec>& hits);
    std::vector<std::unique_ptr<DefFile>> owned;   // files ItemXL makes (its own DLC)
};

// The copies of a record a delta changes: the first copy of each set (items / items_plus), plus later copies with
// on_conflict="replace" (they replace the record in the game). Other later copies are fragments the game merges in.
std::vector<Rec> targets(const std::vector<Rec>& hits);

struct Conflict {
    std::string field, mod_a, val_a, mod_b, val_b;
};

struct Ledger {
    struct Last { std::string mod, kind, value; };
    std::map<std::string, Last> last;
    std::vector<Conflict> conflicts;
    std::vector<std::string> notes, errors, warnings;
    std::vector<std::string> wheres;   // $where: what each filter matched (for the report)
    void write(const std::vector<std::string>& fid, const std::string& mod, const std::string& kind,
               const std::string& value);
};

void records_of(DefFile& f, const std::function<void(Node* parent, Node* el)>& fn);
std::pair<std::string, std::string> key_of(const Node& el);   // (attribute, value) or empty

bool glob_match(const std::string& pattern, const std::string& s);   // * and ?, no case
bool where_match(const Node& el, const Value& filter, std::string& error);
void apply_delta(Index& index, const Value& delta, const std::string& mod, Ledger& led);
// per-file delta of a whole-file override; records missing from the mod are kept (not deleted) and listed in
// missing_in_mod if given, so a mod made for an older game patch cannot silently delete records added by a game update.
Value diff_files(const DefFile& vanilla, const DefFile& mod, std::vector<std::string>* missing_in_mod = nullptr);
Value merge_file_deltas(const std::vector<std::pair<std::string, Value>>& per_file);

// TweakXL syntax: $instances, tagged values -> ItemXL operations.
Value expand(const Value& delta);
// $sell / $loot / $recipe: taken out of the item records; returns the records they stand for (applied right after).
// prefix(lang): the game's word for a diagram in that language ("Schema: "), "" when unknown.
// $alchemy: the game's recipe a new one follows (its recipe and recipe item, what it brews, its cookedItemType)
struct AlchemyLike { std::string recipe, cooked_item, cooked_type; };
// a recipe $alchemy made: the brewed item, the recipe (record and recipe item share the name), the recipe it follows
struct AlchemyMade { std::string item, recipe, like_recipe, like_item, type; };
// diagrams: (item, diagram) of each $recipe whose diagram gets its name from the item (diagram_names)
// alchemy_like(name, out): the game's recipe with that name, or the one that brews the item of that name
Value shortcuts(Value& delta, const std::function<std::string(const std::string&)>& prefix = nullptr,
                std::vector<std::pair<std::string, std::string>>* diagrams = nullptr,
                const std::function<bool(const std::string&, AlchemyLike&)>& alchemy_like = nullptr,
                std::vector<AlchemyMade>* alchemy = nullptr, std::vector<std::string>* errors = nullptr);

// ------------------------------------------------------------ validation ----
// What a YAML delta names must exist (like TweakXL's log). The names come from the game's own files.
struct Catalog {
    std::set<std::string> item_attrs, item_children, stats, crafters, levels;
    explicit Catalog(const std::map<std::string, DefFile*>& files);
};
std::set<std::pair<std::string, std::string>> touched(const Value& delta);   // (tag, name) a delta names
std::vector<std::string> validate(Index& index, const Catalog& cat,
                                  const std::set<std::pair<std::string, std::string>>& records,
                                  const std::map<std::pair<std::string, std::string>, std::string>& mod_of);

// ------------------------------------------------------------- w3strings ----
// The game's text files: read the tables, decode a text when it is asked for (w3strings.cpp).
struct W3Strings {
    Bytes data;
    uint32_t version = 0, magic = 0;
    bool bytes = false;     // texts as UTF-8 bytes (Remastered); else UTF-16 units
    size_t start = 0;
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> entries;   // id -> (offset, length)
    std::map<uint32_t, uint32_t> ids_by_hash;                     // key hash -> id
    bool load(const std::string& path);
    bool text(uint32_t id, std::string& out) const;
    bool by_key(const std::string& key, std::string& out) const;
};
uint32_t w3strings_key_hash(const std::string& key);
// The most common prefix of the game's diagram names ("Schema: "), "" when none.
std::string diagram_prefix(const W3Strings& s, const std::vector<std::string>& schematic_keys);
// Warnings for new items (in ItemXL's DLC) that miss what the game needs to show or equip them.
std::vector<std::string> check_new_items(Index& index);

// ItemXL's own DLC: new records go to <root>\data\gameplay\items(_plus)\itemxl.xml, mounted by <root>\dlcitemxl.reddlc.
#define ITEMXL_DLC_ID "dlcitemxl"
#define ITEMXL_DLC_ROOT "dlc\\dlcitemxl"
#define ITEMXL_REDDLC_PATH "dlc\\dlcitemxl\\dlcitemxl.reddlc"
Bytes build_reddlc(const std::string& dlc_id, const std::vector<std::pair<std::string, std::string>>& mounters);
// templates: also mount dlc\dlcitemxl\data\items\ for the copied templates of $appearance
Bytes itemxl_reddlc(bool templates = false);
// $text -> localisation keys; texts go to sink[lang][key].
// `description: !append {en: ..., de: ...}` adds a line to the text the record has after all mods (append_texts).
struct TextAppend {
    std::string tag, name, field, mod;
    std::vector<std::pair<std::string, std::string>> lines;   // lang -> text
};
using Texts = std::map<std::string, std::map<std::string, std::string>>;
// a $text field: the record, the field (name / description) and the key ItemXL gave it
struct TextUse { std::string tag, name, field, key; };
void take_texts(Value& delta, Texts& sink, std::vector<TextAppend>* appends = nullptr, const std::string& mod = "",
                std::vector<TextUse>* uses = nullptr);
// game_text(lang, key, out): the game's own text. The record gets a key of its own with the old text plus the lines.
// after all mods: each $alchemy recipe item's name in every language: the word the game's recipe items of that kind
// (cookedItemType) start with most often in that language ("Manuscript page: ", "Torn-out page: ", "Formula: ") and
// the new item's final name. pages: (localisation key, cookedItemType) of the game's recipe items
void alchemy_names(Index& index, const std::vector<AlchemyMade>& made, Texts& texts,
                   const std::function<bool(const std::string&, const std::string&, std::string&)>& game_text,
                   const std::vector<std::pair<std::string, std::string>>& pages);
// after all mods: each diagram's name in every language, the game's word for a diagram plus the item's final name
void diagram_names(Index& index, const std::vector<std::pair<std::string, std::string>>& diagrams, Texts& texts,
                   const std::function<bool(const std::string&, const std::string&, std::string&)>& game_text,
                   const std::function<std::string(const std::string&)>& prefix);
void append_texts(Index& index, const std::vector<TextAppend>& appends, Texts& texts,
                  const std::function<bool(const std::string&, const std::string&, std::string&)>& game_text,
                  Ledger& led);
// The records a delta makes ($base / $xml of a record that does not exist yet), taken out of it: the build makes
// all new records of all mods first, then applies the changes, so only equal values depend on the load order.
Value split_new(Index& index, Value& delta);

// ------------------------------------------------------------------ cr2w ----
// The game's resource format (.w2ent, .w2mesh, .xbm): cr2w.cpp
struct Cr2w {
    uint32_t version = 164, flags = 0, build = 0, table_count = 6;
    uint64_t timestamp = 0;
    Bytes strings;
    std::vector<std::string> names;
    std::vector<uint32_t> hashes;
    struct Import { std::string path, cls; uint16_t flags; };
    std::vector<Import> imports;
    std::vector<Bytes> properties;
    struct Export { std::string cls; uint16_t flags; uint32_t parent, tmpl; Bytes data; };
    std::vector<Export> exports;
    struct Buffer { uint32_t flags, index, disk, mem, crc; Bytes data; bool external; };
    std::vector<Buffer> buffers;
    bool read(const Bytes& data, std::string& error);
    Bytes write();
    uint16_t name(const std::string& n);                // index of a name, added when new
    std::pair<size_t, size_t> find_prop(const Export& e, const std::string& prop) const;
    bool set_prop(Export& e, const std::string& prop, const Bytes& value);
    bool remove_prop(Export& e, const std::string& prop);
};
Bytes cr2w_retarget(const Bytes& data, const std::map<std::string, std::string>& paths, int& changed, std::string& error);

// --------------------------------------------------------------- texture ----
// DDS -> .xbm + texture.cache entry: texture.cpp
struct TextureInfo {
    uint32_t width = 0, height = 0;
    int format = 0;                          // cache format: 7 DXT1, 8 DXT5, 10 BC7, 14 BC4, 15 BC5, 253 RGBA
    std::string compression, group;          // .xbm: ETextureCompression, texture group
    int resident_mip = 0;                    // first mip kept in the .xbm (longer side 32 or less)
    struct Mip { uint32_t width, height, pitch; };
    std::vector<Mip> mips;
    std::vector<uint32_t> sizes;             // bytes per mip
};
struct CacheTexture { std::string name; uint32_t key; TextureInfo info; std::vector<Bytes> pieces; };
// The cache key of a texture: the game's string hash (h * 31 + c) of its lower-case depot path. The game finds
// icons by this hash of their path (an entry with another key is never found); a .xbm carries its key.
uint32_t texture_key(const std::string& path);
bool read_dds(const Bytes& data, const std::string& name, TextureInfo& info, Bytes& pixels, std::string& error);
std::vector<Bytes> texture_pieces(const TextureInfo& info, const Bytes& pixels);
Bytes make_xbm(const TextureInfo& info, uint32_t cache_key, const Bytes& resident);
Bytes write_texture_cache(const std::vector<CacheTexture>& textures);

// ------------------------------------------------------------ appearance ----
// $appearance: items with templates, models and textures of their own (appearance.cpp)
struct AppearanceReq { std::string item, mod; Value spec; };
struct ModFile { std::string mod, path, disk; };          // depot path, file on disk (<mod>\itemxl\files\...)
struct AppearanceOut {
    std::map<std::string, Bytes> files;                   // depot path -> bytes, into ItemXL's content
    std::vector<CacheTexture> textures;                   // into ItemXL's texture.cache
    bool templates = false;                               // the .reddlc needs the templates mounter
};
// find_template(name, bytes, path): a template (.w2ent) by its file name without extension
using TemplateFinder = std::function<bool(const std::string&, Bytes&, std::string&)>;
std::string appearance_name(const std::string& item);     // the copy's template name: itemxl_<item>
void take_appearances(Value& delta, std::vector<AppearanceReq>& out, const std::string& mod);
void build_appearances(Index& index, const std::vector<AppearanceReq>& reqs, const std::vector<ModFile>& files,
                       const TemplateFinder& find_template,
                       const std::function<bool(const std::string&)>& game_has, AppearanceOut& out, Ledger& led,
                       std::vector<std::string>& warn);

// ------------------------------------------------------------------ pack ----

std::vector<std::string> walk_order(const std::vector<std::string>& paths);
// Writes blob0.bundle and metadata.store into content_dir.
bool write_content(const std::string& content_dir, const std::map<std::string, Bytes>& files, std::string& error);

// ----------------------------------------------------------------- build ----

struct BuildOptions {
    std::string game;            // game folder
    std::string mods_dir;        // usually <game>\Mods
    std::string mods_settings;   // usually Documents\The Witcher 3\mods.settings ("" = none)
    std::string out_mod;         // folder of the output mod, usually <mods_dir>\mod00000_ItemXL
    std::string dlc_dir;         // DLC folders (each with content\); "" = <game>\dlc
    std::string environment;     // TW3SE and game build; part of the stamp, so an update of either rebuilds
    bool auto_import = true;
    bool force = false;          // build even when nothing changed
    std::function<void(const std::string&)> log;
};

struct BuildResult {
    bool built = false;          // false: nothing changed, the old output stays
    bool ok = true;
    int files_written = 0, conflicts = 0, errors = 0;
    std::string report;          // text of itemxl_report.txt
};

BuildResult build(const BuildOptions& opt);
std::string serialize_def(const DefFile& f);
// The WitcherScript part (script.cpp): notifications after loading a save (missing items, build problems).
std::string itemxl_script(const std::string& build_id, int errors, int warnings);   // the text of itemxl.ws

}  // namespace itemxl
