// Live changes: what a new build changed against the one the game has loaded, as one definitions XML that TW3SE
// mounts while the game runs (w3.items.mount, TW3SE 1.4.0 and later).
//
// The game reads definitions through CR4DefinitionsDLCMounter; a mounted copy of a record with
// on_conflict="replace" replaces the record (measured for DLC copies, see docs/contributing/05-game-behavior.md).
// So the delta holds each record that is new or changed, under the same element path as in its file, and a record
// the game already has gets on_conflict="replace" unless it carries an on_conflict of its own.
//
// The New Game Plus sets (items_plus, abilities_plus) go into a second XML: the game mounts them with
// CR4DefinitionsNGPlusDLCMounter, so the plugin does the same, and only in a New Game Plus game.
//
// What a mount cannot do is reported as a note and waits for the next start: a record that is gone, and every file
// that is not a definitions XML (appearances, textures, the .reddlc).
#include "itemxl.hpp"

#include <algorithm>

namespace itemxl {

namespace {

struct LiveRec {
    std::vector<std::string> chain;   // tags from the root down to the record's parent
    const Node* el;
};

using RecMap = std::map<std::pair<std::string, std::string>, LiveRec>;   // (tag, name) -> record

void collect(const Node& n, std::vector<std::string>& chain, RecMap& out) {
    for (auto& c : n.children) {
        auto k = key_of(*c);
        if (!k.first.empty()) {
            out.emplace(std::make_pair(c->tag, k.second), LiveRec{chain, c.get()});
        } else {
            chain.push_back(c->tag);
            collect(*c, chain, out);
            chain.pop_back();
        }
    }
}

bool new_game_plus(const std::string& path) {
    std::string l = lower(path);
    return l.find("_plus\\") != std::string::npos || l.find("_plus.xml") != std::string::npos;
}

std::string text(const Bytes& b) { return std::string(b.begin(), b.end()); }

}  // namespace

LiveDelta live_delta(const std::map<std::string, Bytes>& before, const std::map<std::string, Bytes>& after) {
    LiveDelta d;
    std::map<std::string, const Bytes*> old_by, new_by;   // lower-case path
    for (auto& kv : before) old_by[lower(kv.first)] = &kv.second;
    for (auto& kv : after) new_by[lower(kv.first)] = &kv.second;

    // The game's own files first, then the DLC files: a DLC copy with on_conflict="extend" is laid over the record
    // after it was replaced, as at the start.
    std::vector<std::string> paths;
    for (auto& kv : after) paths.push_back(kv.first);
    std::stable_sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) {
        bool da = starts_with(lower(a), "dlc\\"), db = starts_with(lower(b), "dlc\\");
        return da != db ? db : lower(a) < lower(b);
    });
    for (auto& kv : before)
        if (!new_by.count(lower(kv.first))) d.notes.push_back(kv.first + ": no longer made (takes effect at the next start)");

    // the delta documents: <redxml><definitions>... with the records under their paths; the New Game Plus files
    // (items_plus, abilities_plus) go into a document of their own, mounted by the New Game Plus mounter
    auto root = std::make_unique<Node>(), root_plus = std::make_unique<Node>();
    root->tag = root_plus->tag = "redxml";
    auto place = [&](Node* base, const std::vector<std::string>& chain) -> Node* {
        Node* at = base;
        // the chain starts below the file's root element (redxml)
        for (auto& t : chain) {
            Node* next = at->find(t);
            if (!next) {
                auto n = std::make_unique<Node>();
                n->tag = t;
                next = n.get();
                at->children.push_back(std::move(n));
            }
            at = next;
        }
        return at;
    };

    for (auto& path : paths) {
        const Bytes& now = *new_by[lower(path)];
        auto o = old_by.find(lower(path));
        if (o != old_by.end() && *o->second == now) continue;                     // unchanged file
        if (!is_definition_xml(path)) {
            d.notes.push_back(path + ": not a definitions file (takes effect at the next start)");
            continue;
        }
        bool plus = new_game_plus(path);
        std::string err;
        auto nr = xml_parse(text(now), err);
        if (!nr) { d.notes.push_back(path + ": not parsed (" + err + ")"); continue; }
        std::unique_ptr<Node> orr;
        if (o != old_by.end()) orr = xml_parse(text(*o->second), err);
        RecMap was, is;
        std::vector<std::string> chain;
        if (orr) collect(*orr, chain, was);
        collect(*nr, chain, is);
        for (auto& kv : was)
            if (!is.count(kv.first))
                d.notes.push_back(path + ": " + kv.first.first + " '" + kv.first.second +
                                  "' removed (takes effect at the next start)");
        for (auto& kv : is) {
            auto w = was.find(kv.first);
            std::string now_text = xml_to_string(*xml_stripped(*kv.second.el));
            if (w != was.end() && xml_to_string(*xml_stripped(*w->second.el)) == now_text) continue;
            auto copy = kv.second.el->clone();
            copy->tail.clear();
            if (w != was.end() && !copy->attr("on_conflict")) copy->set_attr("on_conflict", "replace");
            place(plus ? root_plus.get() : root.get(), kv.second.chain)->children.push_back(std::move(copy));
            if (plus) {
                d.records_plus++;
                d.names_plus.push_back(kv.first.first + " '" + kv.first.second + "'");
                d.keys_plus.push_back(kv.first);
            } else {
                d.records++;
                d.names.push_back(kv.first.first + " '" + kv.first.second + "'");
                d.keys.push_back(kv.first);
            }
        }
    }
    auto text_of_root = [](Node& r) {
        xml_indent(r, "\t");
        return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n" + xml_to_string(r) + "\n";
    };
    if (d.records) d.xml = text_of_root(*root);
    if (d.records_plus) d.xml_plus = text_of_root(*root_plus);
    return d;
}

std::map<std::string, Bytes> bundle_files(const std::string& bundle) {
    std::map<std::string, Bytes> out;
    for (auto& e : bundle_entries(bundle)) {
        Bytes b;
        if (bundle_read(e, b)) out[e.name] = std::move(b);
    }
    return out;
}

}  // namespace itemxl
