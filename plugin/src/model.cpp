// Records, deltas and diffs.
//
// A record is an element with a key attribute (name, or name_name for crafting schematics) that does not sit
// inside another record. It is addressed as (tag, key). A delta is a mapping tag -> name -> spec; the spec
// format is described in the README.
#include <tuple>
#include "itemxl.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <regex>
#include <set>

namespace itemxl {

static const char* KEY_ATTRS[] = {"name", "name_name"};
static const char* LIST_OPS[] = {"!append", "!append-once", "!prepend", "!prepend-once", "!remove", "!set"};
static const char* ADD_OPS[] = {"!append", "!append-once", "!prepend", "!prepend-once"};
static const char* ENTRY_KEYS[] = {"name", "item_name"};

static std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t k = 0; k < v.size(); k++) out += (k ? sep : "") + v[k];
    return out;
}

static bool is_list_op(const std::string& k) {
    for (auto* o : LIST_OPS) if (k == o) return true;
    return false;
}

// An operation's value as a list: one value is a list of one, nothing is an empty list.
static std::vector<Value> lst(const Value* v) {
    if (!v || v->is_null()) return {};
    if (v->is_list()) return v->list;
    return {*v};
}

std::pair<std::string, std::string> key_of(const Node& el) {
    for (auto* k : KEY_ATTRS)
        if (auto* v = el.attr(k)) return {k, *v};
    return {"", ""};
}

void records_of(DefFile& f, const std::function<void(Node*, Node*)>& fn) {
    std::function<void(Node*)> walk = [&](Node* parent) {
        // copy the pointers: fn may not change the tree, but keep the walk independent of it
        std::vector<Node*> kids;
        for (auto& c : parent->children) kids.push_back(c.get());
        for (Node* ch : kids) {
            if (!key_of(*ch).first.empty()) fn(parent, ch);
            else walk(ch);
        }
    };
    walk(f.root.get());
}

void Index::build() {
    rec.clear();
    for (auto& kv : files)
        records_of(*kv.second, [&](Node* p, Node* el) { rec[{el->tag, key_of(*el).second}].push_back({kv.second, p, el}); });
    for (auto& kv : rec) sort(kv.second);
}

void Index::sort(std::vector<Rec>& hits) {
    std::stable_sort(hits.begin(), hits.end(), [](const Rec& a, const Rec& b) {
        return std::make_pair(a.file->rank, a.file->order) < std::make_pair(b.file->rank, b.file->order);
    });
}

static bool plus_variant(const std::string& path) {
    auto parts = split(lower(path), '\\');
    for (size_t k = 0; k + 1 < parts.size(); k++)
        if (ends_with(parts[k], "_plus")) return true;
    return false;
}

std::vector<Rec> targets(const std::vector<Rec>& hits) {
    std::vector<Rec> out;
    bool seen[2] = {false, false};
    for (auto& h : hits) {
        int v = plus_variant(h.file->path) ? 1 : 0;
        if (!seen[v]) {
            seen[v] = true;
            out.push_back(h);
        } else if (auto* c = h.el->attr("on_conflict"); c && *c == "replace") {
            out.push_back(h);
        }
    }
    return out;
}

// --------------------------------------------------------------- ledger ----

void Ledger::write(const std::vector<std::string>& fid, const std::string& mod, const std::string& kind,
                   const std::string& value) {
    std::string id = join(fid, "/");
    auto it = last.find(id);
    if (it != last.end() && it->second.mod != mod) {
        auto& p = it->second;
        if (kind == "set" && p.kind == "set" && p.value != value)
            conflicts.push_back({id, p.mod, p.value, mod, value});
        else if (kind == "set" && p.kind != "set")
            notes.push_back(id + ": " + mod + " sets '" + value + "' over the " + p.kind + " of " + p.mod);
        else if (kind != "set")
            notes.push_back(id + ": " + mod + " " + kind + " stacks on " + p.mod);
    }
    last[id] = {mod, kind, value};
}

// -------------------------------------------------------- list children ----

static std::string child_kind(const Node& el) {
    if (el.tag == "tags") return "tags";
    if (el.children.empty()) return "leaf";
    bool flat = std::all_of(el.children.begin(), el.children.end(), [](auto& k) { return k->children.empty(); });
    if (flat) {
        if (std::all_of(el.children.begin(), el.children.end(),
                        [](auto& k) { return k->attrs.empty() && !trim(k->text).empty(); }))
            return "strings";
        if (std::all_of(el.children.begin(), el.children.end(), [](auto& k) { return k->attr("item_name") != nullptr; }))
            return "counted_attr";
        if (std::all_of(el.children.begin(), el.children.end(),
                        [](auto& k) { return !trim(k->text).empty() && k->attrs.size() == 1; }))
            return "counted_text";
    }
    return "raw";
}

using Counted = std::vector<std::pair<std::string, std::string>>;   // ordered name -> quantity

static std::vector<std::string> read_strings(const Node& el, const std::string& kind) {
    std::vector<std::string> out;
    if (kind == "tags") {
        for (auto& t : split(el.text, ','))
            if (!trim(t).empty()) out.push_back(trim(t));
    } else {
        for (auto& k : el.children) out.push_back(trim(k->text));
    }
    return out;
}

static Counted read_counted(const Node& el, const std::string& kind) {
    Counted out;
    auto put = [&](const std::string& n, const std::string& q) {
        for (auto& kv : out) if (kv.first == n) { kv.second = q; return; }
        out.emplace_back(n, q);
    };
    for (auto& k : el.children) {
        if (kind == "counted_attr") {
            std::string q = "None";
            for (auto& a : k->attrs) if (a.first != "item_name") { q = a.second; break; }
            put(*k->attr("item_name"), q);
        } else {
            put(trim(k->text), k->attrs.empty() ? "None" : k->attrs[0].second);
        }
    }
    return out;
}

static void write_strings(Node& el, const std::string& kind, const std::vector<std::string>& v, const Node* tmpl) {
    if (kind == "tags") {
        el.text = " " + join(v, ", ") + " ";
        return;
    }
    std::string sub = tmpl ? tmpl->tag : "a";
    el.children.clear();
    for (auto& s : v) {
        auto n = std::make_unique<Node>();
        n->tag = sub;
        n->text = s;
        el.children.push_back(std::move(n));
    }
}

static void write_counted(Node& el, const std::string& kind, const Counted& v, const Node* tmpl) {
    std::string sub = tmpl ? tmpl->tag : "parts";
    std::string attr;
    if (tmpl)
        for (auto& a : tmpl->attrs) if (a.first != "item_name") { attr = a.first; break; }
    el.children.clear();
    for (auto& kv : v) {
        auto n = std::make_unique<Node>();
        n->tag = sub;
        if (kind == "counted_attr") {
            n->attrs.emplace_back(attr.empty() ? "quantity" : attr, kv.second);
            n->attrs.emplace_back("item_name", kv.first);
        } else {
            n->attrs.emplace_back(attr.empty() ? "count" : attr, kv.second);
            n->text = kv.first;
        }
        el.children.push_back(std::move(n));
    }
}

// --------------------------------------------------------- named entries ----

static std::string entry_key(const Node& el) {
    for (auto* k : ENTRY_KEYS) if (el.attr(k)) return k;
    return "";
}

static std::set<std::string> keyed_tags(const Node& el) {
    std::map<std::string, std::vector<const Node*>> groups;
    std::vector<std::string> order;
    for (auto& c : el.children) {
        if (!groups.count(c->tag)) order.push_back(c->tag);
        groups[c->tag].push_back(c.get());
    }
    std::set<std::string> out;
    for (auto& t : order) {
        auto& chs = groups[t];
        bool all = std::all_of(chs.begin(), chs.end(), [](const Node* c) { return !entry_key(*c).empty() && c->children.empty(); });
        if (all && (chs.size() > 1 || ends_with(t, "_entry"))) out.insert(t);
    }
    return out;
}

static bool is_keyed_group(const Node& el, const std::string& key, const Value& val) {
    if (keyed_tags(el).count(key)) return true;
    std::vector<Value> adds;
    for (auto* op : ADD_OPS) for (auto& x : lst(val.get(op))) adds.push_back(x);
    return !adds.empty() && std::all_of(adds.begin(), adds.end(), [](const Value& x) { return x.is_map(); });
}

// A name the XML writer may put down as a tag or an attribute. Anything else (a key with a space, say) would make
// the whole merged file unreadable for the game, with every record in it.
static bool xml_name(const std::string& s) {
    if (s.empty()) return false;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        bool ok = std::isalpha(c) || c == '_' || c >= 0x80 || (i && (std::isdigit(c) || c == '-' || c == '.' || c == ':'));
        if (!ok) return false;
    }
    return true;
}

// The first key of a mapping that is no XML name, or nullptr.
static const std::string* bad_name(const Map& m) {
    for (auto& kv : m) if (!xml_name(kv.first)) return &kv.first;
    return nullptr;
}

static void apply_keyed(Node& el, const std::string& tag, const Value& val, std::vector<std::string> fid,
                        const std::string& mod, Ledger& led) {
    auto find = [&](const std::string& name) {
        std::vector<Node*> out;
        for (auto& c : el.children)
            if (c->tag == tag) { auto* v = c->attr(entry_key(*c)); if (v && *v == name) out.push_back(c.get()); }
        return out;
    };
    auto remove = [&](Node* c) { el.children.erase(el.children.begin() + el.index_of(c)); };
    auto with = [&](const std::string& x) { auto f = fid; f.push_back(x); return f; };

    const Node* tmpl = nullptr;
    for (auto& c : el.children) if (c->tag == tag) { tmpl = c.get(); break; }
    std::string keyattr = tmpl ? entry_key(*tmpl) : "name";
    std::unique_ptr<Node> tmpl_copy = tmpl ? tmpl->clone() : nullptr;   // the template may be removed below

    for (auto& name : lst(val.get("!remove"))) {
        for (Node* c : find(name.text())) remove(c);
        led.write(with(name.text()), mod, "remove", "None");
    }

    for (auto* op : ADD_OPS) {
        for (auto& entry : lst(val.get(op))) {
            if (!entry.is_map() || (!entry.has(keyattr) && !entry.has("name"))) {
                led.errors.push_back(mod + ": " + join(fid, "/") + ": " + op + " needs entries with '" + keyattr + "'");
                continue;
            }
            Map rest;
            std::string name = entry.has(keyattr) ? entry.get(keyattr)->text() : entry.get("name")->text();
            for (auto& kv : entry.map)
                if (kv.first != keyattr && kv.first != "name") rest.push_back(kv);
            if (const std::string* b = bad_name(rest)) {
                led.errors.push_back(mod + ": " + join(fid, "/") + ": '" + *b + "' is not a valid attribute name");
                continue;
            }
            auto hits = find(name);
            if (!hits.empty()) {
                for (Node* c : hits) for (auto& kv : rest) c->set_attr(kv.first, kv.second.text());
            } else {
                auto n = std::make_unique<Node>();
                n->tag = tag;
                n->attrs.emplace_back(keyattr, name);
                if (tmpl_copy)
                    for (auto& a : tmpl_copy->attrs) if (a.first != keyattr) n->set_attr(a.first, a.second);
                for (auto& kv : rest) n->set_attr(kv.first, kv.second.text());
                std::vector<int> same;
                for (size_t k = 0; k < el.children.size(); k++) if (el.children[k]->tag == tag) same.push_back((int)k);
                if (starts_with(op, "!prepend") && !same.empty())
                    el.children.insert(el.children.begin() + same.front(), std::move(n));
                else if (!same.empty())
                    el.children.insert(el.children.begin() + same.back() + 1, std::move(n));
                else
                    el.children.push_back(std::move(n));
            }
            Value rv; rv.type = Value::Dict; rv.map = rest;
            led.write(with(name), mod, "set", to_json(rv));
        }
    }

    for (auto& kv : val.map) {
        if (is_list_op(kv.first)) continue;
        auto hits = find(kv.first);
        const Value& patch = kv.second;
        if (patch.is_null()) {
            for (Node* c : hits) remove(c);
        } else if (hits.empty()) {
            led.errors.push_back(mod + ": " + join(fid, "/") + ": entry '" + kv.first + "' not found (use !append to add it)");
            continue;
        } else if (const std::string* b = bad_name(patch.map)) {
            led.errors.push_back(mod + ": " + join(fid, "/") + ": '" + *b + "' is not a valid attribute name");
            continue;
        } else {
            for (Node* c : hits)
                for (auto& a : patch.map) {
                    if (a.second.is_null()) c->remove_attr(a.first);
                    else c->set_attr(a.first, a.second.text());
                }
        }
        led.write(with(kv.first), mod, "set", patch.is_null() ? "None" : to_json(patch));
    }
}

// --------------------------------------------------------------- apply ----

static bool parse_num(const std::string& s, double& out) {
    std::string t = trim(s);
    if (t.empty()) return false;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && *end == 0;
}

static void apply_num(Node& n, const std::vector<std::string>& keys, const std::string& op, double amount) {
    for (auto& k : keys) {
        if (auto* v = n.attr(k)) {
            double x;
            if (!parse_num(*v, x)) continue;
            n.set_attr(k, fmt_double(op == "!scale" ? x * amount : x + amount));
        }
    }
}

static double num_of(const Value& v) {
    if (v.type == Value::Int) return (double)v.i;
    if (v.type == Value::Float) return v.f;
    if (v.type == Value::Bool) return v.b;
    double x = 0;
    parse_num(v.text(), x);
    return x;
}

static void apply_record(Node& el, const Value& spec, const std::vector<std::string>& rid, const std::string& mod,
                         Ledger& led) {
    auto with = [](std::vector<std::string> f, const std::string& x) { f.push_back(x); return f; };
    for (auto& kv : spec.map) {
        const std::string& key = kv.first;
        const Value& val = kv.second;
        if (starts_with(key, "$")) continue;
        if (!xml_name(key)) {
            led.errors.push_back(mod + ": " + join(rid, "/") + ": '" + key + "' is not a valid attribute or element name");
            continue;
        }
        auto fid = with(rid, key);
        Node* child = el.find(key);

        // numeric ops
        if (val.is_map() && (val.has("!scale") || val.has("!offset"))) {
            std::string op = val.has("!scale") ? "!scale" : "!offset";
            double amount = num_of(*val.get(op));
            if (child) apply_num(*child, {"min", "max", "value"}, op, amount);
            else if (el.attr(key)) apply_num(el, {key}, op, amount);
            else {
                led.errors.push_back(mod + ": " + join(rid, "/") + ": " + key + " not found for " + op);
                continue;
            }
            led.write(fid, mod, op.substr(1), val.get(op)->text());
            continue;
        }

        // raw replacement
        if (val.is_map() && val.has("!xml")) {
            std::string err;
            auto n = xml_parse(val.get("!xml")->text(), err);
            if (!n) { led.errors.push_back(mod + ": " + join(rid, "/") + ": " + key + ": bad !xml: " + err); continue; }
            if (child) {
                int idx = el.index_of(child);
                el.children[(size_t)idx] = std::move(n);
            } else {
                el.children.push_back(std::move(n));
            }
            led.write(fid, mod, "set", val.get("!xml")->text());
            continue;
        }

        // removal
        if (val.is_null()) {
            if (child) el.children.erase(el.children.begin() + el.index_of(child));
            else el.remove_attr(key);
            led.write(fid, mod, "set", "None");
            continue;
        }

        // named entries (<loot_entry name=".."/>)
        if (val.is_map() && is_keyed_group(el, key, val)) {
            apply_keyed(el, key, val, fid, mod, led);
            continue;
        }

        // lists: tags, <a> lists
        std::string kind = child ? child_kind(*child) : "";
        bool is_list_spec = val.is_list();
        if (val.is_map())
            for (auto* o : LIST_OPS) if (val.has(o)) is_list_spec = true;
        if (kind == "tags" || kind == "strings" || (!child && is_list_spec)) {
            if (!child) {
                auto n = std::make_unique<Node>();
                n->tag = key;
                el.children.push_back(std::move(n));
                child = el.children.back().get();
                kind = key == "tags" ? "tags" : "strings";
            }
            auto items = read_strings(*child, kind);
            std::unique_ptr<Node> tmpl = child->children.empty() ? nullptr : child->children[0]->clone();
            auto texts = [](const std::vector<Value>& vs) {
                std::vector<std::string> o;
                for (auto& v : vs) o.push_back(v.text());
                return o;
            };
            if (val.is_list()) {
                items = texts(val.list);
                led.write(fid, mod, "set", join(items, ", "));
            } else {
                if (val.has("!set")) {
                    items = texts(lst(val.get("!set")));
                    led.write(fid, mod, "set", join(items, ", "));
                }
                for (auto& v : lst(val.get("!remove"))) {
                    std::string t = v.text();
                    items.erase(std::remove(items.begin(), items.end(), t), items.end());
                    led.write(with(fid, t), mod, "remove", t);
                }
                for (auto* op : ADD_OPS) {
                    bool once = ends_with(op, "-once") || kind == "tags";
                    bool pre = starts_with(op, "!prepend");
                    auto vals = texts(lst(val.get(op)));
                    if (pre) std::reverse(vals.begin(), vals.end());
                    std::string kname = std::string(op).substr(1);
                    if (ends_with(kname, "-once")) kname = kname.substr(0, kname.size() - 5);
                    for (auto& v : vals) {
                        if (once && std::find(items.begin(), items.end(), v) != items.end()) continue;
                        if (pre) items.insert(items.begin(), v);
                        else items.push_back(v);
                        led.write(with(fid, v), mod, kname, v);
                    }
                }
            }
            write_strings(*child, kind, items, tmpl.get());
            continue;
        }

        // counted lists: ingredients, recycling parts
        if (kind == "counted_attr" || kind == "counted_text") {
            auto items = read_counted(*child, kind);
            std::unique_ptr<Node> tmpl = child->children.empty() ? nullptr : child->children[0]->clone();
            Value entries;
            if (val.is_map()) entries = val.has("!set") ? *val.get("!set") : val;
            if (val.is_map() && val.has("!set")) {
                items.clear();
                led.write(fid, mod, "set", to_json(entries));
            }
            for (auto& e : entries.map) {
                auto it = std::find_if(items.begin(), items.end(), [&](auto& p) { return p.first == e.first; });
                if (e.second.is_null()) {
                    if (it != items.end()) items.erase(it);
                } else if (it != items.end()) {
                    it->second = e.second.text();
                } else {
                    items.emplace_back(e.first, e.second.text());
                }
                led.write(with(fid, e.first), mod, "set", e.second.text());
            }
            write_counted(*child, kind, items, tmpl.get());
            continue;
        }

        // child with attributes (ability stats)
        if (val.is_map()) {
            if (const std::string* b = bad_name(val.map)) {
                led.errors.push_back(mod + ": " + join(fid, "/") + ": '" + *b + "' is not a valid attribute name");
                continue;
            }
            if (!child) {
                auto n = std::make_unique<Node>();
                n->tag = key;
                el.children.push_back(std::move(n));
                child = el.children.back().get();
            }
            for (auto& a : val.map) {
                if (a.second.is_null()) child->remove_attr(a.first);
                else child->set_attr(a.first, a.second.text());
                led.write(with(fid, a.first), mod, "set", a.second.text());
            }
            continue;
        }

        // plain XML attribute
        if (child && kind == "leaf" && child->attrs.empty()) child->text = val.text();
        else el.set_attr(key, val.text());
        led.write(fid, mod, "set", val.text());
    }
}

// --------------------------------------------------------- ItemXL's DLC ----
// New records go to ItemXL's own DLC (reddlc.cpp mounts its two folders). The game reads every XML in
// dlc\dlcitemxl\data\gameplay\items\ (and items_plus\ for New Game Plus), so the game's own files stay untouched.

static std::string itemxl_path(bool plus) {
    return std::string(ITEMXL_DLC_ROOT) + (plus ? "\\data\\gameplay\\items_plus\\itemxl.xml"
                                                : "\\data\\gameplay\\items\\itemxl.xml");
}

static DefFile* itemxl_file(Index& index, bool plus) {
    std::string path = itemxl_path(plus);
    auto it = index.files.find(lower(path));
    if (it != index.files.end()) return it->second;
    auto f = std::make_unique<DefFile>();
    f->path = path;
    f->root = std::make_unique<Node>();
    f->root->tag = "redxml";
    f->rank = 3;                                  // after every file of the game, DLCs and mods
    f->order = 1000000 + (plus ? 1 : 0);
    DefFile* raw = f.get();
    index.files[lower(path)] = raw;
    index.owned.push_back(std::move(f));
    return raw;
}

static std::vector<std::string> containers_of(const std::string& tag) {
    if (tag == "item") return {"definitions", "items"};
    if (tag == "ability") return {"definitions", "abilities"};
    if (tag == "schematic") return {"custom", "crafting_schematics"};   // next to <definitions>, as the game has it
    if (tag == "recipe") return {"custom", "alchemy_recipes"};          // alchemy recipes, the same way
    if (tag == "loot") return {"definitions", "loot_definitions"};
    return {};
}

// The element at root/chain[0]/chain[1]/..., made when missing.
static Node* container(DefFile& f, const std::vector<std::string>& chain) {
    Node* el = f.root.get();
    for (auto& tag : chain) {
        Node* next = el->find(tag);
        if (!next) {
            auto n = std::make_unique<Node>();
            n->tag = tag;
            next = n.get();
            el->children.push_back(std::move(n));
        }
        el = next;
    }
    return el;
}

// Tags from the root down to parent (parent included).
static std::vector<std::string> chain_to(const DefFile& f, const Node* parent) {
    std::vector<std::string> path;
    std::function<bool(const Node*)> walk = [&](const Node* el) {
        if (el == parent) return true;
        for (auto& c : el->children) {
            path.push_back(c->tag);
            if (walk(c.get())) return true;
            path.pop_back();
        }
        return false;
    };
    walk(f.root.get());
    return path;
}

std::vector<std::string> check_new_items(Index& index) {
    std::vector<std::string> out;
    auto it = index.files.find(lower(itemxl_path(false)));
    if (it == index.files.end()) return out;
    static const char* NEEDS[] = {"category", "icon_path", "localisation_key_name"};
    static const char* EQUIPPED[] = {"steelsword", "silversword", "crossbow", "armor", "gloves", "pants", "boots"};
    records_of(*it->second, [&](Node*, Node* el) {
        if (el->tag != "item") return;
        std::vector<std::string> missing;
        // an item with the tag NoShow is never shown (scabbards and other technical items): it needs no icon or name
        bool hidden = false;
        if (Node* t = el->find("tags")) {
            std::string tag;
            for (char c : t->text + ",") {
                if (c == ',') { hidden |= tag == "NoShow"; tag.clear(); }
                else if (!std::isspace((unsigned char)c)) tag += c;
            }
        }
        for (auto* a : NEEDS) {
            if (hidden && std::string(a) != "category") continue;
            auto* v = el->attr(a);
            if (!v || v->empty()) missing.push_back(a);
        }
        auto* cat = el->attr("category");
        bool equipped = false;
        for (auto* c : EQUIPPED) equipped |= cat && *cat == c;
        auto* et = el->attr("equip_template");
        if (equipped && (!et || et->empty())) missing.push_back("equip_template");
        if (!missing.empty()) {
            std::string m;
            for (size_t k = 0; k < missing.size(); k++) m += (k ? ", " : "") + missing[k];
            out.push_back("new item '" + key_of(*el).second + "' has no " + m);
        }
    });
    return out;
}

static std::vector<Rec> create(Index& index, const std::string& tag, const std::string& name, const Value& spec,
                               const std::string& mod, Ledger& led) {
    std::vector<Rec> created;
    bool has_file = spec.has("$file") && !spec.get("$file")->is_null() && !spec.get("$file")->text().empty();
    if (spec.has("$base")) {
        std::string base_name = spec.get("$base")->text();
        auto base = targets(index.get(tag, base_name));
        if (base.empty()) {
            led.errors.push_back(mod + ": " + tag + "/" + name + ": $base '" + base_name + "' not found");
            return {};
        }
        for (auto& r : base) {
            auto n = r.el->clone();
            n->set_attr(key_of(*r.el).first, name);
            n->remove_attr("on_conflict");                    // a new record replaces nothing
            DefFile* f = itemxl_file(index, plus_variant(r.file->path));
            Node* parent = container(*f, chain_to(*r.file, r.parent));
            Node* raw = n.get();
            parent->children.push_back(std::move(n));
            f->dirty = true;
            created.push_back({f, parent, raw});
        }
    } else if (!has_file) {
        std::string err;
        auto proto = xml_parse(spec.get("$xml")->text(), err);
        if (!proto) { led.errors.push_back(mod + ": " + tag + "/" + name + ": bad $xml: " + err); return {}; }
        auto chain = containers_of(tag);
        if (chain.empty()) {
            led.errors.push_back(mod + ": " + tag + "/" + name + ": a new <" + tag + "> needs $base or $file");
            return {};
        }
        for (bool plus : {false, true}) {                      // the game and New Game Plus
            DefFile* f = itemxl_file(index, plus);
            Node* parent = container(*f, chain);
            auto n = proto->clone();
            Node* raw = n.get();
            parent->children.push_back(std::move(n));
            f->dirty = true;
            created.push_back({f, parent, raw});
        }
    } else {
        std::string err;
        auto proto = xml_parse(spec.get("$xml")->text(), err);
        if (!proto) { led.errors.push_back(mod + ": " + tag + "/" + name + ": bad $xml: " + err); return {}; }
        std::string target = spec.has("$file") ? spec.get("$file")->text() : "None";
        auto it = index.files.find(lower(target));   // keys are lower case
        if (it == index.files.end()) {
            led.errors.push_back(mod + ": " + tag + "/" + name + ": $file '" + target + "' is not a known definition file");
            return {};
        }
        DefFile* f = it->second;
        Node* parent = nullptr;
        records_of(*f, [&](Node* p, Node* e) { if (!parent && e->tag == tag) parent = p; });
        if (!parent) {
            led.errors.push_back(mod + ": " + tag + "/" + name + ": " + f->path + " has no <" + tag + "> records to sit next to");
        } else {
            auto n = proto->clone();
            Node* raw = n.get();
            parent->children.push_back(std::move(n));
            f->dirty = true;
            created.push_back({f, parent, raw});
        }
    }
    for (auto& c : created) index.get(tag, name).push_back(c);
    index.sort(index.get(tag, name));
    led.write({tag, name}, mod, "create", spec.has("$base") ? spec.get("$base")->text() : "$xml");
    return created;
}

// !append-from / !prepend-from (as in TweakXL): the entries of the same list of other records (same tag), as they are
// at this point of the build, become a plain !append / !prepend. The copy of the same set (items / items_plus) is read.
static Value resolve_from(Index& index, const std::string& tag, const Value& spec, const std::string& path,
                          const std::vector<std::string>& rid, const std::string& mod, Ledger& led) {
    static const std::pair<const char*, const char*> FROM[] = {{"!append-from", "!append"}, {"!prepend-from", "!prepend"}};
    auto has_from = [](const Value& v) { return v.is_map() && (v.has("!append-from") || v.has("!prepend-from")); };
    if (!std::any_of(spec.map.begin(), spec.map.end(), [&](auto& kv) { return has_from(kv.second); })) return spec;
    bool plus = plus_variant(path);
    std::string rs = join(rid, "/");
    auto error = [&](const std::string& m) {                 // once, not per copy
        if (std::find(led.errors.begin(), led.errors.end(), m) == led.errors.end()) led.errors.push_back(m);
    };
    Value out;
    out.type = Value::Dict;
    for (auto& kv : spec.map) {
        if (!has_from(kv.second)) { out.map.push_back(kv); continue; }
        Value val = kv.second;
        for (auto& ft : FROM) {
            std::vector<Value> names;
            auto it = std::find_if(val.map.begin(), val.map.end(), [&](auto& p) { return p.first == ft.first; });
            if (it != val.map.end()) { names = lst(&it->second); val.map.erase(it); }
            std::vector<Value> got;
            for (auto& nv : names) {
                std::string n = nv.text();
                auto r = index.rec.find({tag, n});
                std::vector<Rec> hits = r == index.rec.end() ? std::vector<Rec>{} : targets(r->second);
                const Rec* src = nullptr;
                for (auto& h : hits) if (plus_variant(h.file->path) == plus) { src = &h; break; }
                if (!src && !hits.empty()) src = &hits[0];
                if (!src) { error(mod + ": " + rs + ": " + kv.first + ": " + ft.first + " '" + n + "' does not exist"); continue; }
                Node* child = src->el->find(kv.first);
                if (!child) continue;
                std::string kind = child_kind(*child);
                if (kind != "tags" && kind != "strings") {
                    error(mod + ": " + rs + ": " + kv.first + ": " + ft.first + " works for lists of names only");
                    continue;
                }
                for (auto& s : read_strings(*child, kind)) got.push_back(Value::str(s));
            }
            if (!got.empty()) {
                Value l;
                l.type = Value::List;
                l.list = lst(val.get(ft.second));
                for (auto& g : got) l.list.push_back(g);
                val.set(ft.second, l);
            }
        }
        if (!val.map.empty()) out.map.emplace_back(kv.first, val);
    }
    return out;
}

// ---------------------------------------------------------------- extend ----
// A DLC copy with on_conflict="extend" is laid over the record by the game: the fields it sets win. So a change
// also goes to every such copy that sets the field itself, or the copy would put its own value back. Fields the copy
// does not set stay away from it. Lists, as the game merges them (checked in the game, 11.11.2026):
//   tags            joined with the record's: an !append on the record is enough; !remove and a whole new list go
//                   to the copy too, or the copy would bring the tag back
//   base_abilities  the copy's list replaces the record's: the whole change goes to the copy
// Other lists in such a copy are left alone and reported.
static void apply_extends(Index& index, const std::string& tag, const std::string& name, const std::vector<Rec>& done,
                          const Value& spec, const std::vector<std::string>& rid, const std::string& mod, Ledger& led) {
    for (auto& r : index.get(tag, name)) {
        if (std::any_of(done.begin(), done.end(), [&](const Rec& d) { return d.el == r.el; })) continue;
        const std::string* oc = r.el->attr("on_conflict");
        if (!oc || lower(*oc) != "extend") continue;
        Value part;
        part.type = Value::Dict;
        std::vector<std::string> lists;
        for (auto& kv : spec.map) {
            if (starts_with(kv.first, "$")) continue;
            if (r.el->attr(kv.first)) { part.map.push_back(kv); continue; }
            if (Node* ch = r.el->find(kv.first)) {
                std::string k = child_kind(*ch);
                if (k == "leaf" || k == "raw" || kv.first == "base_abilities") part.map.push_back(kv);
                else if (k == "tags") {
                    if (!kv.second.is_map()) { part.map.push_back(kv); continue; }     // a whole new list
                    Value only;
                    only.type = Value::Dict;
                    for (auto& op : kv.second.map) if (op.first == "!remove") only.map.push_back(op);
                    if (!only.map.empty()) part.map.emplace_back(kv.first, only);
                } else lists.push_back(kv.first);
            }
        }
        std::string rs = join(rid, "/");
        if (!part.map.empty()) {
            apply_record(*r.el, resolve_from(index, tag, part, r.file->path, rid, mod, led), rid, mod, led);
            r.file->dirty = true;
            std::string fields;
            for (auto& kv : part.map) fields += (fields.empty() ? "" : ", ") + kv.first;
            led.notes.push_back(mod + ": " + rs + ": " + fields + " also set by " + r.file->path +
                                " (on_conflict=\"extend\"); ItemXL's value applies there too");
        }
        for (auto& l : lists)
            led.warnings.push_back(mod + ": " + rs + ": " + l + " is a list in " + r.file->path +
                                   " (on_conflict=\"extend\"); the change may not reach the game");
    }
}

// ---------------------------------------------------------------- $where ----
// A filter instead of a name: the change goes to every record of the tag that matches. Keys:
//   name      a pattern (* and ?), or a list of patterns (any)       category  a value or a list (any)
//   tags      all of them      tags_any  at least one      tags_none  none of them
//   ability   base abilities the record has (all)               has       child elements it has (all), e.g. stats
//   any other key: an attribute, compared as text or with a leading >, >=, <, <=, =, != (numbers when both are)
// Letters are compared without case.

bool glob_match(const std::string& pat, const std::string& s) {
    std::string p = lower(pat), t = lower(s);
    size_t pi = 0, ti = 0, star = std::string::npos, mark = 0;
    while (ti < t.size()) {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) { pi++; ti++; }
        else if (pi < p.size() && p[pi] == '*') { star = pi++; mark = ti; }
        else if (star != std::string::npos) { pi = star + 1; ti = ++mark; }
        else return false;
    }
    while (pi < p.size() && p[pi] == '*') pi++;
    return pi == p.size();
}

static std::vector<std::string> texts_of(const Value& v) {
    std::vector<std::string> out;
    if (v.is_list()) for (auto& x : v.list) out.push_back(x.text());
    else if (!v.is_null()) out.push_back(v.text());
    return out;
}

static bool compare_text(const std::string& have, const std::string& want_in) {
    std::string want = trim(want_in), op = "=";
    for (const char* o : {">=", "<=", "!=", ">", "<", "="})
        if (starts_with(want, o)) { op = o; want = trim(want.substr(std::strlen(o))); break; }
    char* e1 = nullptr;
    char* e2 = nullptr;
    double a = std::strtod(have.c_str(), &e1), b = std::strtod(want.c_str(), &e2);
    bool num = !have.empty() && !want.empty() && *e1 == 0 && *e2 == 0;
    int c = num ? (a < b ? -1 : a > b ? 1 : 0) : (lower(have) < lower(want) ? -1 : lower(have) > lower(want) ? 1 : 0);
    if (op == "=") return c == 0;
    if (op == "!=") return c != 0;
    if (op == ">") return c > 0;
    if (op == ">=") return c >= 0;
    if (op == "<") return c < 0;
    return c <= 0;
}

bool where_match(const Node& el, const Value& w, std::string& err) {
    if (!w.is_map()) { err = "$where must be a mapping"; return false; }
    std::set<std::string> tags;
    if (const Node* t = const_cast<Node&>(el).find("tags"))
        for (auto& x : read_strings(*t, "tags")) tags.insert(lower(x));
    std::set<std::string> abilities;
    if (const Node* a = const_cast<Node&>(el).find("base_abilities"))
        for (auto& x : read_strings(*a, "strings")) abilities.insert(lower(x));
    for (auto& kv : w.map) {
        const std::string& k = kv.first;
        auto vals = texts_of(kv.second);
        if (k == "name") {
            const std::string* n = el.attr("name");
            if (!n || !std::any_of(vals.begin(), vals.end(), [&](auto& p) { return glob_match(p, *n); })) return false;
        } else if (k == "category") {
            const std::string* c = el.attr("category");
            if (!c || !std::any_of(vals.begin(), vals.end(), [&](auto& p) { return lower(p) == lower(*c); })) return false;
        } else if (k == "tags") {
            for (auto& v : vals) if (!tags.count(lower(v))) return false;
        } else if (k == "tags_any") {
            if (!std::any_of(vals.begin(), vals.end(), [&](auto& v) { return tags.count(lower(v)) > 0; })) return false;
        } else if (k == "tags_none") {
            for (auto& v : vals) if (tags.count(lower(v))) return false;
        } else if (k == "ability") {
            for (auto& v : vals) if (!abilities.count(lower(v))) return false;
        } else if (k == "has") {
            for (auto& v : vals) if (!const_cast<Node&>(el).find(v)) return false;
        } else if (starts_with(k, "$")) {
            err = "$where: unknown key " + k;
            return false;
        } else {
            const std::string* a = el.attr(k);
            if (!a || !std::all_of(vals.begin(), vals.end(), [&](auto& v) { return compare_text(*a, v); })) return false;
        }
    }
    return true;
}

static void apply_where(Index& index, const std::string& tag, const std::string& label, const Value& spec,
                        const std::string& mod, Ledger& led) {
    Value rest;
    rest.type = Value::Dict;
    for (auto& kv : spec.map) if (kv.first != "$where") rest.map.push_back(kv);
    std::vector<std::string> names;
    std::string err;
    for (auto& kv : index.rec) {
        if (kv.first.first != tag || kv.second.empty()) continue;
        if (where_match(*kv.second.front().el, *spec.get("$where"), err)) names.push_back(kv.first.second);
        if (!err.empty()) { led.errors.push_back(mod + ": " + tag + " '" + label + "': " + err); return; }
    }
    if (names.empty()) {
        led.warnings.push_back(mod + ": " + tag + " '" + label + "': $where matches no record");
        return;
    }
    std::string shown;
    for (size_t i = 0; i < names.size() && i < 5; i++) shown += (i ? ", " : "") + names[i];
    if (names.size() > 5) shown += ", ...";
    led.wheres.push_back(mod + ": " + tag + " '" + label + "': " + std::to_string(names.size()) + " record(s): " + shown);
    for (auto& n : names) {
        std::vector<std::string> rid{tag, n};
        auto tg = targets(index.get(tag, n));
        for (auto& r : tg) {
            apply_record(*r.el, resolve_from(index, tag, rest, r.file->path, rid, mod, led), rid, mod, led);
            r.file->dirty = true;
        }
        apply_extends(index, tag, n, tg, rest, rid, mod, led);
    }
}

void apply_delta(Index& index, const Value& delta, const std::string& mod, Ledger& led) {
    if (!delta.is_map()) { led.errors.push_back(mod + ": a delta must be a mapping"); return; }
    for (auto& tkv : delta.map) {
        const std::string& tag = tkv.first;
        if (!tkv.second.is_map()) {
            led.errors.push_back(mod + ": '" + tag + "' must be a mapping of record names");
            continue;
        }
        for (auto& rkv : tkv.second.map) {
            std::string name = rkv.first, only;
            size_t bar = name.find(" | ");
            if (bar != std::string::npos) { only = name.substr(bar + 3); name = name.substr(0, bar); }
            const Value& spec = rkv.second;
            if (spec.is_map() && spec.has("$where")) { apply_where(index, tag, rkv.first, spec, mod, led); continue; }
            std::vector<std::string> rid{tag, name};
            auto& all = index.get(tag, name);
            std::vector<Rec> hits;
            for (auto& r : all)
                if (only.empty() || lower(r.file->path) == lower(only)) hits.push_back(r);
            if (only.empty()) hits = targets(hits);

            auto drop_hits = [&]() {
                all.erase(std::remove_if(all.begin(), all.end(), [&](const Rec& r) {
                    return std::any_of(hits.begin(), hits.end(), [&](const Rec& h) { return h.el == r.el; });
                }), all.end());
            };

            if (spec.is_null()) {
                for (auto& r : hits) {
                    r.parent->children.erase(r.parent->children.begin() + r.parent->index_of(r.el));
                    r.file->dirty = true;
                }
                drop_hits();
                led.write(rid, mod, "set", "None");
                continue;
            }
            if (!spec.is_map()) {
                led.errors.push_back(mod + ": " + tag + "/" + name + ": expected a mapping");
                continue;
            }
            if (hits.empty() && (spec.has("$base") || spec.has("$xml"))) {
                hits = create(index, tag, name, spec, mod, led);
            } else if (!hits.empty() && spec.has("$xml")) {
                std::vector<Rec> replaced;
                std::string err;
                for (auto& r : hits) {
                    auto n = xml_parse(spec.get("$xml")->text(), err);
                    if (!n) break;
                    Node* raw = n.get();
                    r.parent->children[(size_t)r.parent->index_of(r.el)] = std::move(n);
                    r.file->dirty = true;
                    replaced.push_back({r.file, r.parent, raw});
                }
                if (!err.empty()) { led.errors.push_back(mod + ": " + tag + "/" + name + ": bad $xml: " + err); continue; }
                drop_hits();
                for (auto& r : replaced) all.push_back(r);
                index.sort(all);
                hits = replaced;
                led.write(rid, mod, "set", "$xml");
            }
            if (hits.empty()) {
                led.errors.push_back(mod + ": " + tag + "/" + name + " does not exist (add $base or $xml to create it)");
                continue;
            }
            for (auto& r : hits) {
                apply_record(*r.el, resolve_from(index, tag, spec, r.file->path, rid, mod, led), rid, mod, led);
                r.file->dirty = true;
            }
            if (only.empty()) apply_extends(index, tag, name, hits, spec, rid, mod, led);
        }
    }
}

// ----------------------------------------------------------------- diff ----

// A number when the text reads as one ("5" -> 5, "0.30" -> 0.3), the text otherwise.
static Value numify(const std::string& s) {
    double f;
    std::string t = trim(s);
    std::string lt = lower(t);
    bool special = lt == "inf" || lt == "+inf" || lt == "-inf" || lt == "infinity" || lt == "+infinity" ||
                   lt == "-infinity" || lt == "nan" || lt == "+nan" || lt == "-nan";
    bool ok = !t.empty() && std::regex_match(t, std::regex("[-+]?(\\d+\\.?\\d*|\\.\\d+)([eE][-+]?\\d+)?"));
    if (!ok && !special) return Value::str(s);
    f = std::strtod(t.c_str(), nullptr);
    if (special) f = lt.find("nan") != std::string::npos ? NAN : (lt[0] == '-' ? -INFINITY : INFINITY);
    Value v;
    if (std::isfinite(f) && std::floor(f) == f && s.find('.') == std::string::npos) {
        v.type = Value::Int;
        v.i = (long long)f;
    } else {
        v.type = Value::Float;
        v.f = f;
    }
    return v;
}

static Value xml_value(const Node& n) { return Value::str(xml_to_string(*xml_stripped(n))); }

static Value one(const std::string& k, Value v) { Value m; m.set(k, std::move(v)); return m; }

static Value diff_keyed(const Node& v, const Node& m, const std::string& tag) {
    auto entries = [&](const Node& el) {
        std::vector<std::pair<std::string, const Node*>> out;
        for (auto& c : el.children) {
            if (c->tag != tag) continue;
            auto* k = c->attr(entry_key(*c));
            std::string key = k ? *k : "None";
            if (std::none_of(out.begin(), out.end(), [&](auto& p) { return p.first == key; })) out.emplace_back(key, c.get());
        }
        return out;
    };
    auto find = [](auto& list, const std::string& k) -> const Node* {
        for (auto& p : list) if (p.first == k) return p.second;
        return nullptr;
    };
    auto old = entries(v), now = entries(m);
    Value spec;
    spec.type = Value::Dict;
    Value added;
    added.type = Value::List;
    for (auto& p : now) {
        if (find(old, p.first)) continue;
        Value e;
        std::string ka = entry_key(*p.second);
        e.set(ka.empty() ? "name" : ka, Value::str(p.first));
        for (auto& a : p.second->attrs) {
            bool is_key = false;
            for (auto* k : ENTRY_KEYS) is_key |= a.first == k;
            if (!is_key) e.set(a.first, numify(a.second));
        }
        added.list.push_back(e);
    }
    if (!added.list.empty()) spec.set("!append", added);
    Value gone;
    gone.type = Value::List;
    for (auto& p : old) if (!find(now, p.first)) gone.list.push_back(Value::str(p.first));
    if (!gone.list.empty()) spec.set("!remove", gone);
    for (auto& p : old) {
        const Node* n = find(now, p.first);
        if (!n || n->attrs == p.second->attrs) continue;
        std::set<std::string> names;
        for (auto& a : p.second->attrs) names.insert(a.first);
        for (auto& a : n->attrs) names.insert(a.first);
        Value patch;
        patch.type = Value::Dict;
        for (auto& a : names) {
            const std::string* x = n->attr(a);
            const std::string* o = p.second->attr(a);
            if ((x ? *x : "\x01") == (o ? *o : "\x01")) continue;
            patch.set(a, x ? numify(*x) : Value());
        }
        spec.set(p.first, patch);
    }
    return spec;
}

static Value diff_record(const Node& v, const Node& m) {
    Value none;
    if (xml_canon(v) == xml_canon(m)) return none;
    Value spec;
    spec.type = Value::Dict;
    std::set<std::string> names;
    for (auto& a : v.attrs) names.insert(a.first);
    for (auto& a : m.attrs) names.insert(a.first);
    for (auto& a : names) {
        if (a == "name" || a == "name_name") continue;
        const std::string* mv = m.attr(a);
        const std::string* vv = v.attr(a);
        if (!mv) spec.set(a, Value());
        else if (!vv || *vv != *mv) spec.set(a, numify(*mv));
    }

    auto keyed = keyed_tags(v);
    for (auto& t : keyed_tags(m)) keyed.insert(t);
    std::vector<std::string> vtags, mtags;
    for (auto& c : v.children) if (!keyed.count(c->tag)) vtags.push_back(c->tag);
    for (auto& c : m.children) if (!keyed.count(c->tag)) mtags.push_back(c->tag);
    auto has_dupes = [](std::vector<std::string> t) {
        std::sort(t.begin(), t.end());
        return std::adjacent_find(t.begin(), t.end()) != t.end();
    };
    if (has_dupes(vtags) || has_dupes(mtags)) return one("$xml", xml_value(m));

    for (auto& tag : keyed) {          // std::set: sorted, like Python's sorted()
        Value d = diff_keyed(v, m, tag);
        if (!d.map.empty()) spec.set(tag, d);
    }

    std::vector<std::string> order;
    for (auto& t : vtags) if (std::find(order.begin(), order.end(), t) == order.end()) order.push_back(t);
    for (auto& t : mtags) if (std::find(order.begin(), order.end(), t) == order.end()) order.push_back(t);

    for (auto& tag : order) {
        Node* vc = v.find(tag);
        Node* mc = m.find(tag);
        if (!mc) { spec.set(tag, Value()); continue; }
        if (vc && xml_canon(*vc) == xml_canon(*mc)) continue;
        std::string kind = child_kind(vc ? *vc : *mc);
        std::string mkind = child_kind(*mc);
        bool counted_m = mkind == "strings" || mkind == "counted_attr" || mkind == "counted_text";
        if (vc && kind != mkind && !(kind == "leaf" && counted_m)) {
            spec.set(tag, one("!xml", xml_value(*mc)));
            continue;
        }
        if (kind == "tags" || kind == "strings") {
            auto old = vc ? read_strings(*vc, kind) : std::vector<std::string>{};
            auto now = read_strings(*mc, kind);
            Value op;
            op.type = Value::Dict;
            Value add, rem;
            add.type = rem.type = Value::List;
            for (auto& x : now) if (std::find(old.begin(), old.end(), x) == old.end()) add.list.push_back(Value::str(x));
            for (auto& x : old) if (std::find(now.begin(), now.end(), x) == now.end()) rem.list.push_back(Value::str(x));
            if (!add.list.empty()) op.set("!append", add);
            if (!rem.list.empty()) op.set("!remove", rem);
            if (op.map.empty()) {
                Value all;
                all.type = Value::List;
                for (auto& x : now) all.list.push_back(Value::str(x));
                op.set("!set", all);
            }
            spec.set(tag, op);
        } else if (kind == "counted_attr" || kind == "counted_text" || mkind == "counted_attr" || mkind == "counted_text") {
            std::string k = mkind != "leaf" ? mkind : kind;
            Counted old = (vc && !vc->children.empty()) ? read_counted(*vc, k) : Counted{};
            Counted now = read_counted(*mc, k);
            Value op;
            op.type = Value::Dict;
            for (auto& p : now) {
                auto it = std::find_if(old.begin(), old.end(), [&](auto& q) { return q.first == p.first; });
                if (it == old.end() || it->second != p.second) op.set(p.first, numify(p.second));
            }
            for (auto& p : old)
                if (std::none_of(now.begin(), now.end(), [&](auto& q) { return q.first == p.first; })) op.set(p.first, Value());
            spec.set(tag, op);
        } else if (kind == "leaf" && trim(mc->text).empty()) {
            bool lost = vc && std::any_of(vc->attrs.begin(), vc->attrs.end(), [&](auto& a) { return !mc->attr(a.first); });
            if (lost) {
                spec.set(tag, one("!xml", xml_value(*mc)));
            } else {
                Value op;
                op.type = Value::Dict;
                for (auto& a : mc->attrs) {
                    const std::string* o = vc ? vc->attr(a.first) : nullptr;
                    if (!vc || !o || *o != a.second) op.set(a.first, numify(a.second));
                }
                spec.set(tag, op);
            }
        } else {
            spec.set(tag, one("!xml", xml_value(*mc)));
        }
    }
    return spec;
}

Value diff_files(const DefFile& vanilla, const DefFile& mod, std::vector<std::string>* missing_in_mod) {
    using Key = std::pair<std::string, std::string>;
    std::vector<std::pair<Key, Node*>> vrec, mrec;
    auto collect = [](const DefFile& f, std::vector<std::pair<Key, Node*>>& out) {
        records_of(const_cast<DefFile&>(f), [&](Node*, Node* e) {
            Key k{e->tag, key_of(*e).second};
            for (auto& p : out) if (p.first == k) { p.second = e; return; }   // dict: the last one wins
            out.emplace_back(k, e);
        });
    };
    collect(vanilla, vrec);
    collect(mod, mrec);
    auto find = [](auto& list, const Key& k) -> Node* {
        for (auto& p : list) if (p.first == k) return p.second;
        return nullptr;
    };
    Value out;
    out.type = Value::Dict;
    auto put = [&](const std::string& tag, const std::string& name, Value v) {
        Value* t = out.get(tag);
        if (!t) { Value m; m.type = Value::Dict; out.set(tag, m); t = out.get(tag); }
        t->set(name, std::move(v));
    };
    for (auto& p : mrec) {
        Node* vel = find(vrec, p.first);
        if (!vel) {
            Value s;
            s.set("$file", Value::str(mod.path));
            s.set("$xml", xml_value(*p.second));
            put(p.first.first, p.first.second, s);
            continue;
        }
        Value d = diff_record(*vel, *p.second);
        if (!d.is_null() && !(d.is_map() && d.map.empty())) put(p.first.first, p.first.second, d);
    }
    if (missing_in_mod) {
        for (auto& p : vrec)
            if (!find(mrec, p.first)) missing_in_mod->push_back(p.first.first + " '" + p.first.second + "'");
    }
    return out;
}

Value merge_file_deltas(const std::vector<std::pair<std::string, Value>>& per_file) {
    struct Seen { std::string tag, name; std::vector<std::pair<std::string, Value>> variants; };
    std::vector<Seen> seen;
    for (auto& pf : per_file)
        for (auto& t : pf.second.map)
            for (auto& r : t.second.map) {
                auto it = std::find_if(seen.begin(), seen.end(), [&](auto& s) { return s.tag == t.first && s.name == r.first; });
                if (it == seen.end()) { seen.push_back({t.first, r.first, {}}); it = seen.end() - 1; }
                it->variants.emplace_back(pf.first, r.second);
            }
    Value out;
    out.type = Value::Dict;
    for (auto& s : seen) {
        Value* t = out.get(s.tag);
        if (!t) { Value m; m.type = Value::Dict; out.set(s.tag, m); t = out.get(s.tag); }
        const Value& first = s.variants[0].second;
        bool same = std::all_of(s.variants.begin(), s.variants.end(), [&](auto& v) { return v.second == first; });
        bool has_file = first.is_map() && first.has("$file") && !first.get("$file")->text().empty();
        if (same && !has_file) t->set(s.name, first);
        else for (auto& v : s.variants) t->set(s.name + " | " + v.first, v.second);
    }
    return out;
}

// -------------------------------------------------------------- syntax ----

static std::vector<Value> as_list(const Value& v) { return v.is_list() ? v.list : std::vector<Value>{v}; }

static Value normalize_value(const Value& v) {
    if (v.type == Value::Op) {
        Value inner = v.op ? *v.op : Value();
        if (v.s == "scale" || v.s == "offset") {
            // tagged scalars come in as text: give numbers back as numbers
            if (inner.type == Value::Str) inner = yaml_resolve_scalar(inner.s);
            return one("!" + v.s, inner);
        }
        Value l;
        l.type = Value::List;
        if (!inner.is_null()) l.list = as_list(inner);
        else l.list = {Value()};
        return one("!" + v.s, l);
    }
    if (v.is_list() && !v.list.empty() &&
        std::all_of(v.list.begin(), v.list.end(), [](const Value& x) { return x.type == Value::Op; })) {
        Value out;
        out.type = Value::Dict;
        for (auto& x : v.list) {
            std::string k = "!" + x.s;
            Value* cur = out.get(k);
            if (!cur) { Value l; l.type = Value::List; out.set(k, l); cur = out.get(k); }
            Value inner = x.op ? *x.op : Value();
            for (auto& e : as_list(inner)) cur->list.push_back(e);
        }
        return out;
    }
    return v;
}

static Value subst(const Value& v, const Map& env) {
    static const std::regex var("\\$\\(([A-Za-z0-9_]+)\\)");
    auto lookup = [&](const std::string& k) -> const Value* {
        for (auto& kv : env) if (kv.first == k) return &kv.second;
        return nullptr;
    };
    auto py_str = [](const Value& x) {
        if (x.type == Value::Bool) return std::string(x.b ? "True" : "False");
        if (x.type == Value::Float) { std::string j = to_json(x); return j; }
        return x.text();
    };
    auto sub_text = [&](const std::string& s) {
        std::string out;
        auto begin = std::sregex_iterator(s.begin(), s.end(), var);
        size_t last = 0;
        for (auto it = begin; it != std::sregex_iterator(); ++it) {
            out += s.substr(last, (size_t)it->position() - last);
            const Value* x = lookup((*it)[1].str());
            out += x ? py_str(*x) : it->str();
            last = (size_t)(it->position() + it->length());
        }
        return out + s.substr(last);
    };
    switch (v.type) {
        case Value::Str: {
            std::smatch m;
            if (std::regex_match(v.s, m, var))
                if (const Value* x = lookup(m[1].str())) return *x;   // keeps numbers numbers
            return Value::str(sub_text(v.s));
        }
        case Value::Dict: {
            Value out;
            out.type = Value::Dict;
            for (auto& kv : v.map) out.set(sub_text(kv.first), subst(kv.second, env));
            return out;
        }
        case Value::List: {
            Value out;
            out.type = Value::List;
            for (auto& x : v.list) out.list.push_back(subst(x, env));
            return out;
        }
        case Value::Op: {
            Value out = v;
            out.op = std::make_shared<Value>(v.op ? subst(*v.op, env) : Value());
            return out;
        }
        default: return v;
    }
}

Value expand(const Value& delta) {
    Value out;
    out.type = Value::Dict;
    for (auto& tkv : delta.map) {
        if (!tkv.second.is_map()) { out.set(tkv.first, tkv.second); continue; }
        Value tgt;
        tgt.type = Value::Dict;
        for (auto& rkv : tkv.second.map) {
            std::vector<std::pair<std::string, Value>> variants{{rkv.first, rkv.second}};
            const Value& spec = rkv.second;
            if (spec.is_map() && spec.has("$instances")) {
                Value body;
                body.type = Value::Dict;
                for (auto& kv : spec.map) if (kv.first != "$instances") body.map.push_back(kv);
                variants.clear();
                for (auto& env : spec.get("$instances")->list) {
                    Value name = subst(Value::str(rkv.first), env.map);
                    variants.emplace_back(name.text(), subst(body, env.map));
                }
            }
            for (auto& vr : variants) {
                Value s = vr.second;
                if (s.is_map())
                    for (auto& kv : s.map)
                        if (!starts_with(kv.first, "$")) kv.second = normalize_value(kv.second);
                tgt.set(vr.first, s);
            }
        }
        out.set(tkv.first, tgt);
    }
    return out;
}

// ------------------------------------------------------------ shortcuts ----
// $sell / $loot / $recipe in an item: the records they need are made from them.

static void add_loot(Value& loot, const std::string& item, const Value& where_in) {
    Value where;
    where.type = Value::Dict;
    if (where_in.type == Value::Str) where.set(where_in.s, Value());
    else if (where_in.is_list()) for (auto& w : where_in.list) where.set(w.text(), Value());
    else if (where_in.is_map()) where = where_in;
    for (auto& kv : where.map) {
        Value entry;
        entry.set("name", Value::str(item));
        Value one;
        one.type = Value::Int;
        one.i = 1;
        entry.set("quantity_min", one);
        entry.set("quantity_max", one);
        // the same in every list: always part of the hand-out, at any level (an added entry would take the values of
        // the list's first entry otherwise)
        Value always, none;
        always.type = none.type = Value::Int;
        always.i = -1;
        entry.set("chance", always);
        entry.set("player_level_min", none);
        entry.set("player_level_max", none);
        if (kv.second.is_map()) for (auto& a : kv.second.map) entry.set(a.first, a.second);
        Value* table = loot.get(kv.first);
        if (!table) {
            Value app;
            app.type = Value::List;
            Value le;
            le.set("!append", app);
            Value t;
            t.set("loot_entry", le);
            loot.set(kv.first, t);
            table = loot.get(kv.first);
        }
        table->get("loot_entry")->get("!append")->list.push_back(entry);
    }
}

static std::unique_ptr<Node> elem(const std::string& tag, const std::vector<std::pair<std::string, std::string>>& attrs) {
    auto n = std::make_unique<Node>();
    n->tag = tag;
    n->attrs = attrs;
    return n;
}

static const char* TEXT_LANGS[] = {"ar", "br", "cn", "cz", "de", "en", "es", "esmx", "fr", "hu", "it", "jp", "kr",
                                   "pl", "ru", "tr", "ua", "zh"};

static std::string slug(const std::string& s);

Value shortcuts(Value& delta, const std::function<std::string(const std::string&)>& prefix,
                std::vector<std::pair<std::string, std::string>>* diagrams,
                const std::function<bool(const std::string&, AlchemyLike&)>& alchemy_like,
                std::vector<AlchemyMade>* alchemy, std::vector<std::string>* errors) {
    Value extra;
    extra.type = Value::Dict;
    Value* items = delta.get("item");
    if (!items || !items->is_map()) return extra;
    Value extra_items, schematics, loot, recipes;
    extra_items.type = schematics.type = loot.type = recipes.type = Value::Dict;
    auto take = [](Value& spec, const std::string& k) {
        auto it = std::find_if(spec.map.begin(), spec.map.end(), [&](auto& kv) { return kv.first == k; });
        Value v = it->second;
        spec.map.erase(it);
        return v;
    };
    auto get_or = [](const Value& m, const std::string& k, Value def) { const Value* v = m.get(k); return v ? *v : def; };
    auto num = [](long long n) { Value v; v.type = Value::Int; v.i = n; return v; };

    for (auto& kv : items->map) {
        Value& spec = kv.second;
        if (!spec.is_map()) continue;
        std::string name = kv.first.substr(0, kv.first.find(" | "));
        for (const char* k : {"$sell", "$loot"})
            if (spec.has(k)) add_loot(loot, name, take(spec, k));
        if (spec.has("$alchemy")) {
            // an alchemy recipe: the recipe record and the recipe item, both "<name> recipe", following a game recipe
            Value a = take(spec, "$alchemy");
            if (!a.is_map()) a.type = Value::Dict;
            std::string like_key = a.has("like") ? a.get("like")->text()
                                 : spec.has("$base") ? spec.get("$base")->text() : std::string();
            AlchemyLike like;
            if (like_key.empty() || !alchemy_like || !alchemy_like(like_key, like)) {
                if (errors)
                    errors->push_back("item/" + name + ": $alchemy: no game recipe to follow" +
                                      (like_key.empty() ? std::string() : " for '" + like_key + "'") +
                                      "; use $base with an item the game brews, or like: <a game recipe>");
            } else {
                std::string rname = name + " recipe";
                std::string key = "itemxl_item_name_" + slug(rname);
                Value ri;
                ri.set("$base", Value::str(like.recipe));
                ri.set("localisation_key_name", Value::str(key));
                if (a.has("price")) ri.set("price", *a.get("price"));
                if (a.has("rarity")) {
                    Value abl;
                    abl.type = Value::List;
                    abl.list.push_back(Value::str(a.get("rarity")->text() + " _Stats"));
                    ri.set("base_abilities", abl);
                }
                extra_items.set(rname, ri);
                auto rec = elem("recipe", {{"name_name", rname},
                                           {"type_name", get_or(a, "group", Value::str("itemxl_" + slug(name))).text()},
                                           {"level", get_or(a, "level", num(1)).text()}, {"cookedItem_name", name},
                                           {"cookedItemType", like.cooked_type},
                                           {"cookedItemQuantity", "1"},   // the game brews by the item's ammo, not this
                                           {"localisation_key_name", key}});
                auto ing = elem("ingredients", {});
                if (const Value* ings = a.get("ingredients"); ings && ings->is_map())
                    for (auto& i : ings->map)
                        ing->children.push_back(elem("ingredient", {{"item_name", i.first}, {"quantity", i.second.text()}}));
                rec->children.push_back(std::move(ing));
                Value rv;
                rv.set("$xml", Value::str(xml_to_string(*rec)));
                recipes.set(rname, rv);
                if (a.has("sell")) add_loot(loot, rname, *a.get("sell"));
                if (alchemy) alchemy->push_back({name, rname, like.recipe, like.cooked_item, like.cooked_type});
            }
        }
        if (!spec.has("$recipe")) continue;
        Value r = take(spec, "$recipe");
        if (!r.is_map()) r.type = Value::Dict;
        std::string level = get_or(r, "level", Value::str("Journeyman")).text();
        std::string sname = name + " schematic";

        auto it = elem("item", {{"name", sname}, {"category", "crafting_schematic"},
                                {"price", get_or(r, "diagram_price", num(10)).text()}, {"weight", "0.1"},
                                {"stackable", "100"}});
        if (const Value* lk = spec.get("localisation_key_name"); lk && !lk->is_null() && !lk->text().empty())
            it->attrs.emplace_back("localisation_key_name", lk->text());
        it->attrs.emplace_back("localisation_key_description", "item_desc_crafting_schematic");
        it->attrs.emplace_back("icon_path", "icons/inventory/quests/ico_recipe.png");
        auto tags = elem("tags", {});
        tags->text = "ReadableItem, mod_crafting";
        it->children.push_back(std::move(tags));
        auto ba = elem("base_abilities", {});
        auto a = elem("a", {});
        a->text = get_or(r, "rarity", Value::str("Magic")).text() + " _Stats";   // the diagram's rarity, as vanilla
        ba->children.push_back(std::move(a));
        it->children.push_back(std::move(ba));
        bool own_name = diagrams && !it->attr("localisation_key_name");
        if (own_name) it->attrs.emplace_back("localisation_key_name", "itemxl_item_name_" + slug(sname));
        Value sitem;
        sitem.set("$xml", Value::str(xml_to_string(*it)));
        // the name follows the item's name in every language (diagram_names, after all mods)
        if (own_name) diagrams->push_back({name, sname});
        extra_items.set(sname, sitem);

        auto sc = elem("schematic", {{"name_name", sname}, {"craftedItem_name", name}, {"craftsmanLevel_name", level},
                                     {"craftsmanType_name", get_or(r, "crafter", Value::str("Smith")).text()},
                                     {"price", get_or(r, "price", num(50)).text()}});
        Value q = get_or(r, "quantity", num(1));
        if (q != num(1)) sc->attrs.emplace_back("craftedItemQuantity", q.text());
        auto ing = elem("ingredients", {});
        if (const Value* ings = r.get("ingredients"); ings && ings->is_map())
            for (auto& i : ings->map) ing->children.push_back(elem("ingredient", {{"quantity", i.second.text()}, {"item_name", i.first}}));
        sc->children.push_back(std::move(ing));
        Value ss;
        ss.set("$xml", Value::str(xml_to_string(*sc)));
        schematics.set(sname, ss);
        if (r.has("sell")) add_loot(loot, sname, *r.get("sell"));
    }
    if (!extra_items.map.empty()) extra.set("item", extra_items);
    if (!schematics.map.empty()) extra.set("schematic", schematics);
    if (!recipes.map.empty()) extra.set("recipe", recipes);
    if (!loot.map.empty()) extra.set("loot", loot);
    return extra;
}

static std::string slug(const std::string& s) {
    std::string out;
    bool us = false;
    for (unsigned char c : lower(s)) {
        if (std::isalnum(c) && c < 0x80) { out += (char)c; us = false; }
        else if (!us) { out += '_'; us = true; }
    }
    while (!out.empty() && out.front() == '_') out.erase(out.begin());
    while (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}


static bool is_known_lang(const std::string& l) {
    for (const char* x : TEXT_LANGS) if (l == x) return true;
    return false;
}

void take_texts(Value& delta, Texts& sink, std::vector<TextAppend>* appends, const std::string& mod,
                std::vector<TextUse>* uses) {
    for (auto& tkv : delta.map) {
        if (!tkv.second.is_map()) continue;
        for (auto& rkv : tkv.second.map) {
            Value& spec = rkv.second;
            if (!spec.is_map() || !spec.has("$text")) continue;
            Value texts = *spec.get("$text");
            spec.map.erase(std::find_if(spec.map.begin(), spec.map.end(), [](auto& kv) { return kv.first == "$text"; }));
            std::string name = rkv.first.substr(0, rkv.first.find(" | "));
            std::string base = slug(name);
            for (auto& f : texts.map) {
                if (f.second.type == Value::Op) {
                    if (f.second.s == "append" && appends && f.second.op) {
                        TextAppend a{tkv.first, name, f.first, mod, {}};
                        const Value& v = *f.second.op;
                        if (v.is_map()) {
                            for (auto& lv : v.map) {
                                std::string l = lower(lv.first);
                                if (is_known_lang(l)) a.lines.emplace_back(l, lv.second.text());
                            }
                        } else {
                            a.lines.emplace_back("en", v.text());
                        }
                        appends->push_back(std::move(a));
                    }
                    continue;
                }
                std::string attr = f.first == "name" ? "localisation_key_name"
                                 : f.first == "description" ? "localisation_key_description" : f.first;
                std::string key = "itemxl_" + tkv.first + "_" + f.first + "_" + base;
                spec.set(attr, Value::str(key));
                if (uses) uses->push_back({tkv.first, name, f.first, key});
                if (f.second.type == Value::Str) sink["en"][key] = f.second.s;
                else if (!f.second.is_map() && !f.second.is_null()) sink["en"][key] = f.second.text();   // a number
                else
                    for (auto& lv : f.second.map) {
                        std::string l = lower(lv.first);
                        if (is_known_lang(l)) sink[l][key] = lv.second.text();
                    }
            }
        }
    }
}

void append_texts(Index& index, const std::vector<TextAppend>& appends, Texts& texts,
                  const std::function<bool(const std::string&, const std::string&, std::string&)>& game_text,
                  Ledger& led) {
    using Key = std::tuple<std::string, std::string, std::string>;
    std::vector<Key> order;
    std::map<Key, std::vector<const TextAppend*>> groups;
    for (auto& a : appends) {
        Key k{a.tag, a.name, a.field};
        if (!groups.count(k)) order.push_back(k);
        groups[k].push_back(&a);
    }
    for (auto& k : order) {
        auto& items = groups[k];
        const std::string& tag = std::get<0>(k);
        const std::string& name = std::get<1>(k);
        const std::string& field = std::get<2>(k);
        std::vector<std::string> seen;
        std::string mods;
        for (auto* a : items)
            if (std::find(seen.begin(), seen.end(), a->mod) == seen.end()) {
                mods += (seen.empty() ? "" : ", ") + a->mod;
                seen.push_back(a->mod);
            }
        auto it = index.rec.find({tag, name});
        std::vector<Rec> hits = it == index.rec.end() ? std::vector<Rec>{} : targets(it->second);
        if (hits.empty()) {
            led.errors.push_back(mods + ": " + tag + "/" + name + " does not exist (its $text " + field +
                                 " cannot be appended to)");
            continue;
        }
        std::string attr = field == "name" ? "localisation_key_name"
                         : field == "description" ? "localisation_key_description" : field;
        const std::string* oldp = hits[0].el->attr(attr);
        std::string old = oldp ? *oldp : "";
        std::string key = "itemxl_" + tag + "_" + field + "_more_" + slug(name);
        // An append with `en` adds a line. One without `en` translates: its languages go to the first line with `en`
        // and replace what that line has in them; in mod order, so the later mod wins.
        using Line = std::vector<std::pair<std::string, std::string>>;
        std::vector<Line> merged;
        std::vector<const TextAppend*> plain;
        for (auto* a : items) {
            bool has_en = false;
            for (auto& lv : a->lines) has_en = has_en || lv.first == "en";
            if (has_en) merged.push_back(a->lines);
            else plain.push_back(a);
        }
        for (auto* a : plain)
            for (auto& lv : a->lines) {
                if (!merged.empty()) {
                    auto& m = merged.front();
                    bool put = false;
                    for (auto& x : m)
                        if (x.first == lv.first) { x.second = lv.second; put = true; }   // a later mod replaces it
                    if (!put) m.push_back(lv);
                } else
                    led.warnings.push_back(a->mod + ": " + tag + "/" + name + " $text " + field + " !append " +
                                           lv.first + " has no English line to translate (ignored)");
            }
        for (const char* lang : TEXT_LANGS) {
            std::string base;
            auto find_in = [&](const std::string& l) {
                auto lt = texts.find(l);
                if (lt == texts.end()) return false;
                auto kt = lt->second.find(old);
                if (kt == lt->second.end()) return false;
                base = kt->second;
                return true;
            };
            bool have = find_in(lang) || find_in("en");
            if (!have && !old.empty()) have = game_text(lang, old, base);
            std::string out = have ? base : "";
            for (auto& m : merged) {
                std::string line;
                auto pick = [&](const std::string& l) {
                    for (auto& lv : m)
                        if (lv.first == l) { line = lv.second; return true; }
                    return false;
                };
                if (!pick(lang)) pick("en");
                if (!line.empty()) out += (out.empty() ? "" : "<br>") + line;
            }
            texts[lang][key] = out;
        }
        for (auto& h : hits) {
            h.el->set_attr(attr, key);
            h.file->dirty = true;
        }
    }
}

void diagram_names(Index& index, const std::vector<std::pair<std::string, std::string>>& diagrams, Texts& texts,
                   const std::function<bool(const std::string&, const std::string&, std::string&)>& game_text,
                   const std::function<std::string(const std::string&)>& prefix) {
    for (auto& d : diagrams) {
        auto it = index.rec.find({"item", d.first});
        auto st = index.rec.find({"item", d.second});
        if (it == index.rec.end() || st == index.rec.end()) continue;
        auto items = targets(it->second), diags = targets(st->second);
        if (items.empty() || diags.empty()) continue;
        const std::string* ik = items[0].el->attr("localisation_key_name");
        const std::string* dk = diags[0].el->attr("localisation_key_name");
        if (!ik || ik->empty() || !dk || dk->rfind("itemxl_", 0) != 0) continue;
        auto text_of = [&](const std::string& l, std::string& out) {
            auto lt = texts.find(l);
            if (lt != texts.end()) {
                auto kt = lt->second.find(*ik);
                if (kt != lt->second.end()) { out = kt->second; return true; }
            }
            return game_text(l, *ik, out);
        };
        std::string en;
        text_of("en", en);
        for (const char* l : TEXT_LANGS) {
            std::string t;
            if (!text_of(l, t)) t = en;
            if (t.empty()) continue;
            std::string pat = prefix ? prefix(l) : std::string();
            texts[l][*dk] = (pat.empty() ? std::string("Diagram: ") : pat) + t;
        }
    }
}

// "Manuscript page: Swallow" -> "Manuscript page: " (every recipe item of the game has its word before a colon;
// CJK titles use the full-width colon)
static std::string page_word(const std::string& page) {
    size_t c = page.find(": ");
    size_t w = page.find("\xEF\xBC\x9A");
    if (c != std::string::npos && (w == std::string::npos || c < w)) return page.substr(0, c + 2);
    if (w != std::string::npos) return page.substr(0, w + 3);
    return "";
}

void alchemy_names(Index& index, const std::vector<AlchemyMade>& made, Texts& texts,
                   const std::function<bool(const std::string&, const std::string&, std::string&)>& game_text,
                   const std::vector<std::pair<std::string, std::string>>& pages) {
    auto key_of_item = [&](const std::string& name) -> std::string {
        auto it = index.rec.find({"item", name});
        if (it == index.rec.end()) return "";
        auto hits = targets(it->second);
        const std::string* k = hits.empty() ? nullptr : hits[0].el->attr("localisation_key_name");
        return k ? *k : "";
    };
    auto text_of = [&](const std::string& l, const std::string& key, std::string& out) {
        if (key.empty()) return false;
        auto lt = texts.find(l);
        if (lt != texts.end()) {
            auto kt = lt->second.find(key);
            if (kt != lt->second.end()) { out = kt->second; return true; }
        }
        return game_text(l, key, out);
    };
    // the most common word per (language, kind); a tie goes to the word that sorts first
    std::map<std::pair<std::string, std::string>, std::string> words;
    auto word_for = [&](const std::string& l, const std::string& type) {
        auto k = std::make_pair(l, type);
        auto it = words.find(k);
        if (it != words.end()) return it->second;
        std::map<std::string, int> count;
        for (auto& p : pages) {
            std::string t;
            if (p.second != type || !game_text(l, p.first, t)) continue;
            std::string w = page_word(t);
            if (!w.empty()) count[w]++;
        }
        std::string best;
        int n = 0;
        for (auto& c : count) if (c.second > n) { best = c.first; n = c.second; }
        return words[k] = best;
    };
    for (auto& m : made) {
        std::string ik = key_of_item(m.item), dk = key_of_item(m.recipe);
        if (ik.empty() || dk.rfind("itemxl_", 0) != 0) continue;
        std::string en_name;
        text_of("en", ik, en_name);
        for (const char* l : TEXT_LANGS) {
            std::string name;
            if (!text_of(l, ik, name)) name = en_name;
            if (name.empty()) continue;
            texts[l][dk] = word_for(l, m.type) + name;
        }
    }
}

Value split_new(Index& index, Value& delta) {
    Value made;
    made.type = Value::Dict;
    for (auto& tkv : delta.map) {
        if (!tkv.second.is_map()) continue;
        Value recs;
        recs.type = Value::Dict;
        auto& m = tkv.second.map;
        for (auto it = m.begin(); it != m.end();) {
            const Value& spec = it->second;
            std::string name = it->first.substr(0, it->first.find(" | "));
            auto r = index.rec.find({tkv.first, name});
            bool exists = r != index.rec.end() && !r->second.empty();
            if (spec.is_map() && (spec.has("$base") || spec.has("$xml")) && !exists) {
                recs.map.push_back(std::move(*it));
                it = m.erase(it);
            } else {
                ++it;
            }
        }
        if (!recs.map.empty()) made.map.emplace_back(tkv.first, std::move(recs));
    }
    return made;
}

}  // namespace itemxl

namespace itemxl {

// ------------------------------------------------------------ validation ----
// Like TweakXL's log: what a YAML delta names must exist. What exists is taken from the game's own files (and the
// DLC folders), plus every record the build made; there is no fixed schema for Witcher 3.

Catalog::Catalog(const std::map<std::string, DefFile*>& files) {
    for (auto& kv : files)
        records_of(*kv.second, [&](Node*, Node* el) {
            if (el->tag == "item") {
                for (auto& a : el->attrs) item_attrs.insert(a.first);
                for (auto& c : el->children) item_children.insert(c->tag);
            } else if (el->tag == "ability") {
                for (auto& c : el->children) stats.insert(c->tag);
            } else if (el->tag == "schematic") {
                if (auto* v = el->attr("craftsmanType_name"); v && !v->empty()) crafters.insert(*v);
                if (auto* v = el->attr("craftsmanLevel_name"); v && !v->empty()) levels.insert(*v);
            }
        });
}

std::set<std::pair<std::string, std::string>> touched(const Value& delta) {
    std::set<std::pair<std::string, std::string>> out;
    for (auto& t : delta.map)
        if (t.second.is_map())
            for (auto& r : t.second.map)
                if (!(r.second.is_map() && r.second.has("$where")))     // a filter, not a record name
                    out.insert({t.first, r.first.substr(0, r.first.find(" | "))});
    return out;
}

std::vector<std::string> validate(Index& index, const Catalog& cat,
                                  const std::set<std::pair<std::string, std::string>>& records,
                                  const std::map<std::pair<std::string, std::string>, std::string>& mod_of) {
    std::vector<std::string> out;
    auto exists = [&](const std::string& tag, const std::string& name) {
        auto it = index.rec.find({tag, name});
        return it != index.rec.end() && !it->second.empty();
    };
    auto join_set = [](const std::set<std::string>& s) {
        std::string r;
        for (auto& x : s) r += (r.empty() ? "" : ", ") + x;
        return r;
    };
    for (auto& rid : records) {                          // std::set: sorted like Python's sorted()
        auto it = index.rec.find(rid);
        if (it == index.rec.end()) continue;
        auto hits = targets(it->second);
        if (hits.empty()) continue;                      // removed, or never made: reported where it happened
        Node* el = hits[0].el;
        const std::string& tag = rid.first;
        const std::string& name = rid.second;
        auto m = mod_of.find(rid);
        std::string who = (m == mod_of.end() ? std::string("?") : m->second) + ": " + tag + " '" + name + "'";
        if (tag == "item") {
            for (auto& a : el->attrs)
                if (!cat.item_attrs.count(a.first)) out.push_back(who + ": unknown attribute '" + a.first + "'");
            for (auto& c : el->children)
                if (!cat.item_children.count(c->tag)) out.push_back(who + ": unknown element <" + c->tag + ">");
            if (Node* ba = el->find("base_abilities"))
                for (auto& a : ba->children) {
                    std::string n = trim(a->text);
                    if (!n.empty() && !exists("ability", n)) out.push_back(who + ": base ability '" + n + "' does not exist");
                }
        } else if (tag == "ability") {
            for (auto& c : el->children)
                if (!cat.stats.count(c->tag)) out.push_back(who + ": unknown stat '" + c->tag + "'");
        } else if (tag == "loot") {
            for (auto& c : el->children) {
                auto* n = c->attr("name");
                if (c->tag == "loot_entry" && n && !n->empty() && !exists("item", *n) && !exists("loot", *n))
                    out.push_back(who + ": item '" + *n + "' does not exist");
            }
        } else if (tag == "schematic") {
            if (auto* ci = el->attr("craftedItem_name"); ci && !ci->empty() && !exists("item", *ci))
                out.push_back(who + ": crafted item '" + *ci + "' does not exist");
            if (!exists("item", name)) out.push_back(who + ": no diagram item '" + name + "'");
            for (auto pr : {std::make_pair("craftsmanType_name", &cat.crafters), std::make_pair("craftsmanLevel_name", &cat.levels)}) {
                auto* v = el->attr(pr.first);
                if (v && !v->empty() && !pr.second->count(*v))
                    out.push_back(who + ": " + pr.first + " '" + *v + "' is not one of " + join_set(*pr.second));
            }
            if (Node* ing = el->find("ingredients"))
                for (auto& c : ing->children) {
                    auto* n = c->attr("item_name");
                    if (n && !n->empty() && !exists("item", *n)) out.push_back(who + ": ingredient '" + *n + "' does not exist");
                }
        } else if (tag == "recipe") {
            if (auto* ci = el->attr("cookedItem_name"); ci && !ci->empty() && !exists("item", *ci))
                out.push_back(who + ": brewed item '" + *ci + "' does not exist");
            if (Node* ing = el->find("ingredients"))
                for (auto& c : ing->children) {
                    auto* n = c->attr("item_name");
                    if (n && !n->empty() && !exists("item", *n)) out.push_back(who + ": ingredient '" + *n + "' does not exist");
                }
        }
    }
    return out;
}

}  // namespace itemxl
