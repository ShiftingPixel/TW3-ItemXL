// Tools for writing ItemXL YAML files (itemxl-cli; the plugin uses show for /itemxl show):
//   show    a record as the game has it, every copy with its file
//   search  records by internal name or by the name the player sees
//   schema  a JSON schema for VS Code (YAML extension by Red Hat): completion of every name the game knows, and
//           red underlines for unknown stats, attributes, ingredients, merchants, ... while typing
// What exists comes from the game's own files, as for the checks of a build (Catalog in model.cpp).
#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

#include "browse.hpp"

namespace fs = std::filesystem;

namespace itemxl {

namespace {

std::vector<std::string> dirs_in(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::u8path(dir), ec))
        if (e.is_directory(ec)) out.push_back(e.path().u8string());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> bundles_in(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(dir), ec)) return out;
    for (auto& e : fs::recursive_directory_iterator(fs::u8path(dir), ec))
        if (e.is_regular_file(ec) && lower(e.path().extension().u8string()) == ".bundle") out.push_back(e.path().u8string());
    std::sort(out.begin(), out.end());
    return out;
}

std::string json_str(const std::string& s) {
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o + "\"";
}

}  // namespace

// ------------------------------------------------------------------ game ----

bool GameData::load(const std::string& game, const std::string& dlc_dir, const std::string& lang) {
    std::vector<std::string> bundles;
    for (auto& d : dirs_in(game + "\\content"))
        for (auto& b : bundles_in(d)) bundles.push_back(b);
    size_t first_dlc = bundles.size();
    for (auto& d : dirs_in(dlc_dir.empty() ? game + "\\dlc" : dlc_dir))
        for (auto& b : bundles_in(d + "\\content")) bundles.push_back(b);

    struct Raw { std::string path; Bytes data; bool dlc; int order; };
    std::map<std::string, Raw> raw;
    for (size_t i = 0; i < bundles.size(); i++)
        for (auto& e : bundle_entries(bundles[i])) {
            if (!is_definition_xml(e.name)) continue;
            Bytes data;
            if (!bundle_read(e, data) || !is_definition_data(e.name, data)) continue;
            std::string key = lower(e.name);
            auto it = raw.find(key);
            int order = it == raw.end() ? (int)raw.size() : it->second.order;
            raw[key] = Raw{e.name, std::move(data), i >= first_dlc, order};
        }
    for (auto& kv : raw) {
        std::string err;
        auto root = xml_parse(std::string(kv.second.data.begin(), kv.second.data.end()), err);
        if (!root) continue;
        auto f = std::make_unique<DefFile>();
        f->path = kv.second.path;
        f->root = std::move(root);
        f->rank = kv.second.dlc ? 2 : starts_with(kv.first, "gameplay\\") ? 0 : 1;
        f->order = kv.second.order;
        index.files[kv.first] = f.get();
        store.push_back(std::move(f));
    }
    index.build();
    if (!lang.empty()) has_strings = strings.load(game + "\\content\\content0\\" + lang + ".w3strings");
    return !index.files.empty();
}

void GameData::overlay(const std::string& bundle_path) {
    for (auto& e : bundle_entries(bundle_path)) {
        if (!is_definition_xml(e.name)) continue;
        Bytes data;
        std::string err;
        if (!bundle_read(e, data) || !is_definition_data(e.name, data)) continue;
        auto root = xml_parse(std::string(data.begin(), data.end()), err);
        if (!root) continue;
        std::string key = lower(e.name);
        auto it = index.files.find(key);
        if (it != index.files.end()) {
            it->second->root = std::move(root);
            continue;
        }
        auto f = std::make_unique<DefFile>();
        f->path = e.name;
        f->root = std::move(root);
        f->rank = 2;
        f->order = (int)store.size();
        index.files[key] = f.get();
        store.push_back(std::move(f));
    }
    index.build();
}

std::string GameData::shown_name(const Node& el) const {
    if (!has_strings) return "";
    const std::string* key = el.attr("localisation_key_name");
    std::string out;
    if (key && !key->empty() && strings.by_key(*key, out)) return out;
    return "";
}

// ------------------------------------------------------------ show, search ----

std::string show(GameData& g, const std::string& name, const std::string& tag) {
    std::string out;
    for (auto& kv : g.index.rec) {
        if (kv.first.second != name || kv.second.empty()) continue;
        if (!tag.empty() && kv.first.first != tag) continue;
        auto hits = kv.second;
        g.index.sort(hits);
        for (auto& r : hits) {
            out += "--- " + kv.first.first + " in " + r.file->path;
            std::string shown = g.shown_name(*r.el);
            if (!shown.empty()) out += "   (in game: " + shown + ")";
            auto copy = xml_stripped(*r.el);
            xml_indent(*copy, "  ");
            out += "\n" + xml_to_string(*copy) + "\n";
        }
    }
    return out;
}

std::vector<Found> search(GameData& g, const std::string& text, const std::string& tag) {
    std::string needle = lower(text);
    std::vector<Found> out;
    for (auto& kv : g.index.rec) {
        if (kv.second.empty() || (!tag.empty() && kv.first.first != tag)) continue;
        auto hits = kv.second;
        g.index.sort(hits);
        std::string shown = g.shown_name(*hits[0].el);
        if (lower(kv.first.second).find(needle) == std::string::npos && lower(shown).find(needle) == std::string::npos)
            continue;
        const std::string* cat = hits[0].el->attr("category");
        out.push_back(Found{kv.first.first, kv.first.second, shown, cat ? *cat : "", hits[0].file->path});
    }
    return out;
}

// ---------------------------------------------------------------- schema ----

std::string schema(GameData& g) {
    // what the game knows, by kind
    std::map<std::string, std::string> items, abilities, loots, schematics;   // name -> description
    std::set<std::string> item_attrs, item_children, ability_attrs, stats, crafters, levels, rarities, tags;
    for (auto& kv : g.index.rec) {
        if (kv.second.empty()) continue;
        auto hits = kv.second;
        g.index.sort(hits);
        const Node& el = *hits[0].el;
        const std::string& tag = kv.first.first;
        const std::string& name = kv.first.second;
        if (tag == "item") {
            std::string d = g.shown_name(el);
            if (auto* c = el.attr("category")) d += (d.empty() ? "" : " | ") + *c;
            items[name] = d;
        } else if (tag == "ability") {
            abilities[name] = "";
        } else if (tag == "loot") {
            loots[name] = starts_with(name, "_store_") ? "merchant" : "";
        } else if (tag == "schematic") {
            auto* c = el.attr("craftedItem_name");
            schematics[name] = c ? "makes " + *c : "";
        }
        tags.insert(tag);
    }
    for (auto& kv : g.index.files)
        records_of(*kv.second, [&](Node*, Node* el) {
            if (el->tag == "item") {
                for (auto& a : el->attrs) item_attrs.insert(a.first);
                for (auto& c : el->children) item_children.insert(c->tag);
                auto* cat = el->attr("category");
                if (cat && *cat == "crafting_schematic")
                    if (Node* ba = el->find("base_abilities"))
                        for (auto& a : ba->children) {
                            std::string n = trim(a->text);
                            if (ends_with(n, " _Stats")) rarities.insert(n.substr(0, n.size() - 7));
                        }
            } else if (el->tag == "ability") {
                for (auto& a : el->attrs) ability_attrs.insert(a.first);
                for (auto& c : el->children) stats.insert(c->tag);
            } else if (el->tag == "schematic") {
                if (auto* v = el->attr("craftsmanType_name"); v && !v->empty()) crafters.insert(*v);
                if (auto* v = el->attr("craftsmanLevel_name"); v && !v->empty()) levels.insert(*v);
            }
        });

    auto enum_of = [](const std::set<std::string>& s) {
        std::string o = "{\"enum\": [";
        bool first = true;
        for (auto& x : s) { o += (first ? "" : ", ") + json_str(x); first = false; }
        return o + "]}";
    };
    // a value: one of these, or a $(key) of $instances
    auto value_of = [&](const std::set<std::string>& s) {
        return "{\"anyOf\": [" + enum_of(s) + ", {\"type\": \"string\", \"pattern\": \"\\\\$\\\\(\"}]}";
    };
    // names with a description each (VS Code shows it next to the name in the completion list)
    auto named = [](const std::map<std::string, std::string>& m) {
        std::string names, descs;
        for (auto& kv : m) {
            names += (names.empty() ? "" : ", ") + json_str(kv.first);
            descs += (descs.empty() ? "" : ", ") + json_str(kv.second);
        }
        // or a $(key) of $instances, filled in later
        return "{\"anyOf\": [{\"enum\": [" + names + "],\n      \"markdownEnumDescriptions\": [" + descs +
               "]},\n    {\"type\": \"string\", \"pattern\": \"\\\\$\\\\(\"}]}";
    };
    auto props = [](const std::set<std::string>& s, const std::string& ref) {
        std::string o;
        for (auto& x : s) o += (o.empty() ? "" : ",\n      ") + json_str(x) + ": " + ref;
        return o;
    };
    // record names as properties: completion of existing records; new names are allowed (additionalProperties)
    auto record_map = [](const std::map<std::string, std::string>& m, const std::string& ref) {
        std::string o = "{\"type\": [\"object\", \"null\"], \"properties\": {";
        bool first = true;
        for (auto& kv : m) {
            o += std::string(first ? "\n      " : ",\n      ") + json_str(kv.first) + ": {\"anyOf\": [" + ref +
                 ", {\"type\": \"null\"}]";
            if (!kv.second.empty()) o += ", \"description\": " + json_str(kv.second);
            o += "}";
            first = false;
        }
        return o + "},\n    \"additionalProperties\": {\"anyOf\": [" + ref + ", {\"type\": \"null\"}]}}";
    };

    std::string common =
        "\"$base\": {\"$ref\": \"#/definitions/%BASE%\", \"description\": \"copy of this record: the new record starts "
        "as this one\"},\n"
        "      \"$instances\": {\"type\": \"array\", \"items\": {\"type\": \"object\"}, \"description\": \"one record per "
        "entry; $(key) in names and values is replaced\"},\n"
        "      \"$file\": {\"type\": \"string\"}, \"$xml\": {\"type\": \"string\"},\n"
        "      \"$where\": {\"type\": \"object\", \"description\": \"a filter instead of a name: the change goes to every "
        "record that matches (name, category, tags, tags_any, tags_none, ability, has, or an attribute such as "
        "price: '>500'); the record's key is only a label\"},\n"
        "      \"$if\": {\"description\": \"the record is used only when this holds: {mod: X}, {item: X}, {not: ...}, "
        "{any: [...]}\"}";
    auto with_base = [&](const std::string& b) {
        std::string c = common;
        c.replace(c.find("%BASE%"), 6, b);
        return c;
    };
    std::string langs = "{\"type\": [\"object\", \"string\"], \"properties\": {";
    for (const char* l : {"ar", "br", "cn", "cz", "de", "en", "es", "esmx", "fr", "hu", "it", "jp", "kr", "pl", "ru",
                          "tr", "ua", "zh"})
        langs += std::string(l[0] == 'a' ? "" : ", ") + json_str(l) + ": {\"type\": \"string\"}";
    std::string inner = langs + "}, \"additionalProperties\": false}";
    // `description: !append {...}` (VS Code does not see the tag, the test sees it as a key "!append")
    langs += ", \"!append\": " + inner + "}, \"additionalProperties\": false}";
    std::string where = "{\"anyOf\": [{\"type\": \"array\", \"items\": {\"$ref\": \"#/definitions/loot_name\"}}, "
                        "{\"type\": \"object\", \"propertyNames\": {\"$ref\": \"#/definitions/loot_name\"}}, "
                        "{\"$ref\": \"#/definitions/loot_name\"}]}";

    std::string o;
    o += "{\n  \"$schema\": \"http://json-schema.org/draft-07/schema#\",\n";
    o += "  \"title\": \"ItemXL " ITEMXL_VERSION "\",\n";
    o += "  \"description\": \"ItemXL YAML: what the game knows, read from its own files\",\n";
    o += "  \"type\": \"object\",\n  \"properties\": {\n";
    o += "    \"item\": " + record_map(items, "{\"$ref\": \"#/definitions/item\"}") + ",\n";
    o += "    \"ability\": " + record_map(abilities, "{\"$ref\": \"#/definitions/ability\"}") + ",\n";
    o += "    \"loot\": " + record_map(loots, "{\"type\": \"object\"}") + ",\n";
    o += "    \"schematic\": " + record_map(schematics, "{\"type\": \"object\"}") + ",\n";
    std::string names = "{\"anyOf\": [{\"type\": \"string\"}, {\"type\": \"array\", \"items\": {\"type\": \"string\"}}]";
    o += "    \"$requires\": " + names + ", \"description\": \"mods this file needs; without them it is skipped and "
         "reported\"},\n";
    o += "    \"$after\": " + names + ", \"description\": \"this file comes after these mods, so it wins where both "
         "set one value\"},\n";
    o += "    \"$if\": {\"description\": \"the whole file is used only when this holds: {mod: X}, {item: X}, "
         "{not: ...}, {any: [...]}\"}\n  },\n";
    auto top = tags;
    for (const char* k : {"$requires", "$after", "$if"}) top.insert(k);
    o += "  \"propertyNames\": " + enum_of(top) + ",\n";
    o += "  \"definitions\": {\n";
    o += "    \"item_name\": " + named(items) + ",\n";
    o += "    \"ability_name\": " + named(abilities) + ",\n";
    o += "    \"loot_name\": " + named(loots) + ",\n";
    o += "    \"item\": {\"type\": \"object\", \"additionalProperties\": false, \"properties\": {\n      " +
         with_base("item_name") + ",\n";
    o += "      \"$text\": {\"type\": \"object\", \"additionalProperties\": false, \"properties\": "
         "{\"name\": " + langs + ", \"description\": " + langs + "}},\n";
    o += "      \"$sell\": " + where + ",\n      \"$loot\": " + where + ",\n";
    o += "      \"$recipe\": {\"type\": \"object\", \"additionalProperties\": false, \"required\": [\"ingredients\"], "
         "\"properties\": {\n";
    o += "        \"crafter\": " + value_of(crafters) + ",\n        \"level\": " + value_of(levels) + ",\n";
    o += "        \"rarity\": " + value_of(rarities) + ",\n";
    o += "        \"price\": {\"type\": [\"number\", \"string\"], \"description\": \"what the craftsman takes\"},\n";
    o += "        \"diagram_price\": {\"type\": [\"number\", \"string\"]},\n";
    o += "        \"quantity\": {\"type\": [\"number\", \"string\"]},\n";
    o += "        \"ingredients\": {\"type\": \"object\", \"propertyNames\": {\"$ref\": \"#/definitions/item_name\"}, "
         "\"additionalProperties\": {\"type\": [\"integer\", \"string\", \"null\"]}},\n";
    o += "        \"sell\": " + where + "}},\n";
    o += "      \"$alchemy\": {\"type\": \"object\", \"additionalProperties\": false, \"required\": [\"ingredients\"], "
         "\"description\": \"an alchemy recipe for this item: the recipe and the recipe item '<name> recipe'\", "
         "\"properties\": {\n";
    o += "        \"like\": {\"type\": \"string\", \"description\": \"the game's recipe to follow (default: the recipe "
         "of $base)\"},\n";
    o += "        \"level\": {\"type\": [\"number\", \"string\"]},\n";
    o += "        \"group\": {\"type\": \"string\", \"description\": \"type_name: recipes of one group are levels of "
         "one brew\"},\n";
    o += "        \"price\": {\"type\": [\"number\", \"string\"], \"description\": \"price of the recipe\"},\n";
    o += "        \"rarity\": " + value_of(rarities) + ",\n";
    o += "        \"ingredients\": {\"type\": \"object\", \"propertyNames\": {\"$ref\": \"#/definitions/item_name\"}, "
         "\"additionalProperties\": {\"type\": [\"integer\", \"string\", \"null\"]}},\n";
    o += "        \"sell\": " + where + "}},\n";
    o += "      \"base_abilities\": {\"description\": \"abilities: !append / !remove names\"},\n";
    o += "      " + props(item_attrs, "{}");
    std::set<std::string> children;
    for (auto& c : item_children) if (c != "base_abilities") children.insert(c);
    if (!children.empty()) o += ",\n      " + props(children, "{}");
    o += "}},\n";
    o += "    \"ability\": {\"type\": \"object\", \"additionalProperties\": false, \"properties\": {\n      " +
         with_base("ability_name");
    if (!ability_attrs.empty()) o += ",\n      " + props(ability_attrs, "{}");
    o += ",\n      " + props(stats, "{\"$ref\": \"#/definitions/stat\"}") + "}},\n";
    o += "    \"stat\": {\"description\": \"a number, !scale / !offset, or {type, min, max}\"}\n";
    o += "  }\n}\n";
    return o;
}

}  // namespace itemxl

namespace itemxl {

std::string new_yaml(GameData& g, const std::string& name, const std::string& base, const std::string& tag_in,
                     std::string& error) {
    std::string tag = tag_in.empty() ? "item" : tag_in;
    auto it = g.index.rec.find({tag, base});
    if (it == g.index.rec.end() || it->second.empty()) {
        error = "no " + tag + " '" + base + "' (try: itemxl-cli search)";
        return "";
    }
    const Node& el = *it->second.front().el;
    auto q = [](const std::string& s) {           // a YAML scalar, quoted when it needs to be
        bool plain = !s.empty() && s.find_first_of(":#{}[],&*!|>'\"%@`") == std::string::npos &&
                     s.front() != ' ' && s.back() != ' ' && s.front() != '-' && s.front() != '?';
        if (plain) return s;
        std::string o = "\"";
        for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; }
        return o + "\"";
    };
    std::string shown = g.shown_name(el);
    std::ostringstream y;
    y << "# " << name << ": a new " << tag << " based on " << base;
    if (!shown.empty() && shown != base) y << " (in game: " << shown << ")";
    y << "\n# Made by itemxl-cli new. Lines starting with # are examples: remove the # to use them.\n";
    y << "# Format: docs\\modding of ItemXL. Look up records: itemxl-cli show \"" << base << "\"\n\n";
    y << tag << ":\n  " << q(name) << ":\n    $base: " << q(base) << "\n";
    if (tag == "item") {
        static const char* keys[] = {"price", "weight", "enhancement_slots", "quality", "category", "equip_template"};
        for (const char* k : keys)
            if (const std::string* v = el.attr(k)) y << "    # " << k << ": " << q(*v) << "\n";
        if (Node* ba = el.find("base_abilities")) {
            y << "    # base_abilities:          # abilities of " << base << ":";
            for (auto& c : ba->children) y << " [" << trim(c->text) << "]";
            y << "\n    #   \"!append\": [My ability]\n";
        }
        y << "    $text:\n      name: {en: " << q(name) << "}\n      description: {en: \"\"}\n";
        y << "    # $sell: [merchant loot list]   # find one: itemxl-cli search _store_ --tag loot\n";
    } else {
        for (auto& a : el.attrs)
            if (a.first != "name") y << "    # " << a.first << ": " << q(a.second) << "\n";
        for (auto& c : el.children)
            if (c->attrs.size() && c->attr("min")) y << "    # " << c->tag << ": {type: " << q(c->attr("type") ? *c->attr("type") : "add") << ", min: " << *c->attr("min") << "}\n";
    }
    return y.str();
}

}  // namespace itemxl
