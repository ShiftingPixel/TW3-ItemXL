// A small XML DOM for the game's definition files.
//
// It reads and writes text the way Python's xml.etree.ElementTree does (a well-known, stable layout):
//   - comments and processing instructions are dropped; the text around a comment joins up
//   - line breaks become \n; in attribute values tab, \n and \r become spaces (XML attribute normalization)
//   - element.text is the text before the first child, child.tail the text after the child
//   - writing: <a x="1" /> for an empty element, text escapes & < >, attributes also " \n \r \t
#include "itemxl.hpp"

#include <cctype>
#include <cstring>

namespace itemxl {

// ------------------------------------------------------------------ Node ----

const std::string* Node::attr(const std::string& k) const {
    for (auto& a : attrs)
        if (a.first == k) return &a.second;
    return nullptr;
}

void Node::set_attr(const std::string& k, const std::string& v) {
    for (auto& a : attrs)
        if (a.first == k) { a.second = v; return; }
    attrs.emplace_back(k, v);
}

bool Node::remove_attr(const std::string& k) {
    for (size_t i = 0; i < attrs.size(); i++)
        if (attrs[i].first == k) { attrs.erase(attrs.begin() + (long)i); return true; }
    return false;
}

Node* Node::find(const std::string& t) const {
    for (auto& c : children)
        if (c->tag == t) return c.get();
    return nullptr;
}

int Node::index_of(const Node* child) const {
    for (size_t i = 0; i < children.size(); i++)
        if (children[i].get() == child) return (int)i;
    return -1;
}

std::unique_ptr<Node> Node::clone() const {
    auto n = std::make_unique<Node>();
    n->tag = tag;
    n->attrs = attrs;
    n->text = text;
    n->tail = tail;
    for (auto& c : children) n->children.push_back(c->clone());
    return n;
}

// ---------------------------------------------------------------- parser ----

namespace {

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
    }
}

static const int XML_MAX_DEPTH = 64;

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;
    int dropped = 0;

    explicit Parser(const std::string& src) : s(src) {}

    bool fail(const std::string& m) {
        if (err.empty()) {
            size_t line = 1, col = 1;
            for (size_t k = 0; k < i && k < s.size(); k++) {
                if (s[k] == '\n') { line++; col = 1; }
                else col++;
            }
            err = m + " at line " + std::to_string(line) + ", column " + std::to_string(col) +
                  " (offset " + std::to_string(i) + ")";
        }
        return false;
    }
    bool at(const char* p) const { return s.compare(i, std::strlen(p), p) == 0; }
    static bool name_char(char c) {
        return std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == ':' || (unsigned char)c >= 0x80;
    }
    void skip_ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; }

    // Decode entities in [a, b); attr=true applies attribute-value normalization.
    bool decode(size_t a, size_t b, bool attr, std::string& out) {
        for (size_t k = a; k < b; k++) {
            char c = s[k];
            if (c == '&') {
                size_t semi = s.find(';', k);
                if (semi == std::string::npos || semi >= b) { i = k; return fail("bad entity"); }
                std::string ent = s.substr(k + 1, semi - k - 1);
                if (ent == "lt") out += '<';
                else if (ent == "gt") out += '>';
                else if (ent == "amp") out += '&';
                else if (ent == "quot") out += '"';
                else if (ent == "apos") out += '\'';
                else if (!ent.empty() && ent[0] == '#') {
                    uint32_t cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')
                                      ? (uint32_t)std::strtoul(ent.c_str() + 2, nullptr, 16)
                                      : (uint32_t)std::strtoul(ent.c_str() + 1, nullptr, 10);
                    append_utf8(out, cp);    // a character reference is not normalized
                } else { i = k; return fail("unknown entity &" + ent + ";"); }
                k = semi;
            } else if (c == '\r') {
                if (k + 1 < b && s[k + 1] == '\n') k++;
                out += attr ? ' ' : '\n';
            } else if (attr && (c == '\n' || c == '\t')) {
                out += ' ';
            } else {
                out += c;
            }
        }
        return true;
    }

    // Skips <? ?>, <!-- -->, <!DOCTYPE >. Returns false when the input does not start with one of them.
    bool skip_misc() {
        if (at("<?")) {
            size_t e = s.find("?>", i);
            if (e == std::string::npos) return fail("unclosed <?");
            i = e + 2;
            return true;
        }
        if (at("<!--")) {
            size_t e = s.find("-->", i + 4);
            if (e == std::string::npos) return fail("unclosed comment");
            i = e + 3;
            return true;
        }
        if (at("<!DOCTYPE")) {
            size_t e = s.find('>', i);
            if (e == std::string::npos) return fail("unclosed DOCTYPE");
            i = e + 1;
            return true;
        }
        return false;
    }

    std::unique_ptr<Node> element(int depth = 0) {
        if (depth > XML_MAX_DEPTH) { fail("XML nesting too deep"); return nullptr; }
        // at '<'
        i++;
        size_t a = i;
        while (i < s.size() && name_char(s[i])) i++;
        if (i == a) { fail("element name expected"); return nullptr; }
        auto n = std::make_unique<Node>();
        n->tag = s.substr(a, i - a);
        for (;;) {
            skip_ws();
            if (i >= s.size()) { fail("unclosed tag <" + n->tag); return nullptr; }
            if (s[i] == '/' || s[i] == '>') break;
            size_t na = i;
            while (i < s.size() && name_char(s[i])) i++;
            if (i == na) { fail("attribute name expected"); return nullptr; }
            std::string an = s.substr(na, i - na);
            skip_ws();
            if (i >= s.size() || s[i] != '=') { fail("'=' expected"); return nullptr; }
            i++;
            skip_ws();
            if (i >= s.size() || (s[i] != '"' && s[i] != '\'')) { fail("quote expected"); return nullptr; }
            char q = s[i++];
            size_t va = i;
            size_t ve = s.find(q, i);
            if (ve == std::string::npos) { fail("unclosed attribute value"); return nullptr; }
            std::string v;
            if (!decode(va, ve, true, v)) return nullptr;
            i = ve + 1;
            if (n->attr(an)) dropped++;            // repeated attribute: the first one counts
            else n->attrs.emplace_back(an, v);
        }
        if (s[i] == '/') {
            if (i + 1 >= s.size() || s[i + 1] != '>') { fail("'>' expected"); return nullptr; }
            i += 2;
            return n;
        }
        i++;  // '>'
        std::string* target = &n->text;            // text before the first child, then each child's tail
        for (;;) {
            if (i >= s.size()) { fail("unclosed element <" + n->tag + ">"); return nullptr; }
            if (s[i] == '<') {
                if (at("</")) {
                    i += 2;
                    size_t a2 = i;
                    while (i < s.size() && name_char(s[i])) i++;
                    if (s.compare(a2, i - a2, n->tag) != 0 || i - a2 != n->tag.size()) {
                        fail("mismatched end tag for <" + n->tag + ">");
                        return nullptr;
                    }
                    skip_ws();
                    if (i >= s.size() || s[i] != '>') { fail("'>' expected"); return nullptr; }
                    i++;
                    return n;
                }
                if (at("<![CDATA[")) {
                    size_t e = s.find("]]>", i);
                    if (e == std::string::npos) { fail("unclosed CDATA"); return nullptr; }
                    target->append(s, i + 9, e - i - 9);
                    i = e + 3;
                    continue;
                }
                if (skip_misc()) continue;
                if (!err.empty()) return nullptr;
                auto c = element(depth + 1);
                if (!c) return nullptr;
                n->children.push_back(std::move(c));
                target = &n->children.back()->tail;
                continue;
            }
            size_t a2 = i;
            while (i < s.size() && s[i] != '<') i++;
            if (!decode(a2, i, false, *target)) return nullptr;
        }
    }
};

}  // namespace

// Converts UTF-16 LE/BE (with BOM, as in Next-Gen 4.04 XMLs before the 5.00 UTF-8 transition) to UTF-8.
static std::string to_utf8_if_utf16(const std::string& text) {
    bool le = starts_with(text, "\xFF\xFE");
    bool be = !le && starts_with(text, "\xFE\xFF");
    if (!le && !be) return text;
    std::string out;
    out.reserve(text.size());
    auto rd16 = [&](size_t p) -> uint16_t {
        uint8_t b0 = (uint8_t)text[p], b1 = (uint8_t)text[p + 1];
        return le ? (uint16_t)(b0 | (b1 << 8)) : (uint16_t)((b0 << 8) | b1);
    };
    for (size_t p = 2; p + 1 < text.size(); p += 2) {
        uint32_t cp = rd16(p);
        if (cp >= 0xD800 && cp <= 0xDBFF && p + 3 < text.size()) {
            uint16_t lo = rd16(p + 2);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000u + (((cp - 0xD800u) << 10) | (lo - 0xDC00u));
                p += 2;
            }
        }
        append_utf8(out, cp);
    }
    return out;
}

std::unique_ptr<Node> xml_parse(const std::string& raw_text, std::string& error, int* dropped) {
    std::string text = to_utf8_if_utf16(raw_text);
    Parser p(text);
    if (starts_with(text, "\xEF\xBB\xBF")) p.i = 3;
    std::unique_ptr<Node> root;
    for (;;) {
        p.skip_ws();
        if (p.i >= text.size()) break;
        if (text[p.i] != '<') { p.fail("text outside the root element"); break; }
        if (p.skip_misc()) continue;
        if (!p.err.empty()) break;
        if (root) { p.fail("junk after document element"); break; }
        root = p.element();
        if (!root) break;
    }
    if (!p.err.empty() || !root) {
        error = p.err.empty() ? "no element found" : p.err;
        return nullptr;
    }
    if (dropped) *dropped = p.dropped;
    return root;
}

// ---------------------------------------------------------------- writer ----

static void esc_text(std::string& out, const std::string& t) {
    for (char c : t) {
        if (c == '&') out += "&amp;";
        else if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else out += c;
    }
}

static void esc_attr(std::string& out, const std::string& t) {
    for (char c : t) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\r': out += "&#13;"; break;
            case '\n': out += "&#10;"; break;
            case '\t': out += "&#09;"; break;
            default: out += c;
        }
    }
}

static void write(std::string& out, const Node& n) {
    out += '<';
    out += n.tag;
    for (auto& a : n.attrs) {
        out += ' ';
        out += a.first;
        out += "=\"";
        esc_attr(out, a.second);
        out += '"';
    }
    if (n.text.empty() && n.children.empty()) {
        out += " />";
    } else {
        out += '>';
        esc_text(out, n.text);
        for (auto& c : n.children) write(out, *c);
        out += "</";
        out += n.tag;
        out += '>';
    }
    esc_text(out, n.tail);
}

std::string xml_to_string(const Node& n) {
    std::string out;
    write(out, n);
    return out;
}

// ElementTree.indent(tree, space, level=0)
static void indent_children(Node& e, int level, const std::string& space) {
    std::string child_ind = "\n";
    for (int k = 0; k <= level; k++) child_ind += space;
    if (trim(e.text).empty()) e.text = child_ind;
    for (auto& c : e.children) {
        if (!c->children.empty()) indent_children(*c, level + 1, space);
        if (trim(c->tail).empty()) c->tail = child_ind;
    }
    Node& last = *e.children.back();
    if (trim(last.tail).empty()) {
        std::string ind = "\n";
        for (int k = 0; k < level; k++) ind += space;
        last.tail = ind;
    }
}

void xml_indent(Node& root, const std::string& space) {
    if (!root.children.empty()) indent_children(root, 0, space);
}

std::unique_ptr<Node> xml_stripped(const Node& n) {
    auto c = n.clone();
    std::function<void(Node&)> fix = [&](Node& e) {
        e.text = trim(e.text);
        e.tail.clear();
        for (auto& k : e.children) fix(*k);
    };
    fix(*c);
    return c;
}

std::string xml_canon(const Node& n) { return xml_to_string(*xml_stripped(n)); }

}  // namespace itemxl
