// The YAML subset ItemXL files use, read the way PyYAML's SafeLoader reads it (YAML 1.1).
//
// Supported: block mappings and sequences (also "- key: value" items), flow {..} and [..] (also over several
// lines), plain / 'single' / "double" quoted scalars, | and > block scalars, comments, several documents (---),
// and the tags !append !append-once !prepend !prepend-once !remove !scale !offset !set.
// Anchors and aliases (&name, *name) and merge keys (<<: *name) as in PyYAML; an alias is a copy.
// Not supported (an error names the line): complex keys (?), other tags.
#include <map>
#include "itemxl.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <regex>
#include <sstream>

namespace itemxl {

// ----------------------------------------------------------------- Value ----

const Value* Value::get(const std::string& k) const {
    for (auto& kv : map)
        if (kv.first == k) return &kv.second;
    return nullptr;
}

Value* Value::get(const std::string& k) {
    for (auto& kv : map)
        if (kv.first == k) return &kv.second;
    return nullptr;
}

void Value::set(const std::string& k, Value v) {
    type = Dict;
    for (auto& kv : map)
        if (kv.first == k) { kv.second = std::move(v); return; }
    map.emplace_back(k, std::move(v));
}

// Python repr() of a float: the shortest text that reads back the same number.
static std::string py_repr_double(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    char buf[64];
    for (int prec = 1; prec <= 17; prec++) {
        std::snprintf(buf, sizeof buf, "%.*e", prec - 1, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    // buf is d.ddde±XX; rebuild in Python's style
    std::string m = buf;
    size_t e = m.find('e');
    int exp = std::atoi(m.c_str() + e + 1);
    std::string digits;
    bool neg = m[0] == '-';
    for (size_t k = neg ? 1 : 0; k < e; k++)
        if (m[k] != '.') digits += m[k];
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    std::string out = neg ? "-" : "";
    if (exp < -4 || exp >= 16) {
        out += digits.substr(0, 1);
        if (digits.size() > 1) out += "." + digits.substr(1);
        char eb[16];
        std::snprintf(eb, sizeof eb, "e%c%02d", exp < 0 ? '-' : '+', std::abs(exp));
        out += eb;
    } else if (exp < 0) {
        out += "0." + std::string((size_t)(-exp - 1), '0') + digits;
    } else {
        if ((int)digits.size() <= exp + 1) {
            out += digits + std::string((size_t)(exp + 1 - (int)digits.size()), '0') + ".0";
        } else {
            out += digits.substr(0, (size_t)exp + 1) + "." + digits.substr((size_t)exp + 1);
        }
    }
    return out;
}

std::string Value::text() const {
    switch (type) {
        case Null: return "None";
        case Bool: return b ? "true" : "false";
        case Int: return std::to_string(i);
        case Float: return fmt_double(f);
        case Str: return s;
        case Op: return op ? op->text() : "None";
        default: return to_json(*this);
    }
}

bool Value::operator==(const Value& o) const {
    auto numeric = [](const Value& v) { return v.type == Int || v.type == Float || v.type == Bool; };
    if (numeric(*this) && numeric(o)) {
        double a = type == Int ? (double)i : type == Bool ? (double)b : f;
        double c = o.type == Int ? (double)o.i : o.type == Bool ? (double)o.b : o.f;
        return a == c;
    }
    if (type != o.type) return false;
    switch (type) {
        case Null: return true;
        case Str: return s == o.s;
        case List: return list == o.list;
        case Dict: {
            if (map.size() != o.map.size()) return false;
            for (auto& kv : map) {           // dict equality ignores order
                const Value* x = o.get(kv.first);
                if (!x || *x != kv.second) return false;
            }
            return true;
        }
        case Op: return s == o.s && ((!op && !o.op) || (op && o.op && *op == *o.op));
        default: return false;
    }
}

static void json_str(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
                else out += (char)c;
        }
    }
    out += '"';
}

std::string to_json(const Value& v) {
    std::string out;
    switch (v.type) {
        case Value::Null: return "null";
        case Value::Bool: return v.b ? "true" : "false";
        case Value::Int: return std::to_string(v.i);
        case Value::Float: return py_repr_double(v.f);
        case Value::Str: json_str(out, v.s); return out;
        case Value::List:
            out = "[";
            for (size_t k = 0; k < v.list.size(); k++) out += (k ? ", " : "") + to_json(v.list[k]);
            return out + "]";
        case Value::Dict:
            out = "{";
            for (size_t k = 0; k < v.map.size(); k++) {
                if (k) out += ", ";
                json_str(out, v.map[k].first);
                out += ": " + to_json(v.map[k].second);
            }
            return out + "}";
        case Value::Op:
            out = "{";
            json_str(out, "!" + v.s);
            return out + ": " + (v.op ? to_json(*v.op) : "null") + "}";
    }
    return "null";
}

// --------------------------------------------------- scalar resolution ----

static std::string no_underscores(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
    return s;
}

Value yaml_resolve_scalar(const std::string& t) {
    static const std::regex re_bool("yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF");
    static const std::regex re_int_bin("[-+]?0b[0-1_]+");
    static const std::regex re_int_oct("[-+]?0[0-7_]+");
    static const std::regex re_int_dec("[-+]?(?:0|[1-9][0-9_]*)");
    static const std::regex re_int_hex("[-+]?0x[0-9a-fA-F_]+");
    static const std::regex re_int_sex("[-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+");
    static const std::regex re_float(
        "[-+]?(?:[0-9][0-9_]*)\\.[0-9_]*(?:[eE][-+][0-9]+)?|\\.[0-9_]+(?:[eE][-+][0-9]+)?"
        "|[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+\\.[0-9_]*|[-+]?\\.(?:inf|Inf|INF)|\\.(?:nan|NaN|NAN)");
    Value v;
    if (t.empty() || t == "~" || t == "null" || t == "Null" || t == "NULL") return v;
    if (std::regex_match(t, re_bool)) {
        v.type = Value::Bool;
        char c = t[0];
        v.b = c == 'y' || c == 'Y' || c == 't' || c == 'T' || t == "on" || t == "On" || t == "ON";
        return v;
    }
    auto sign_of = [](std::string& s) {
        int sg = 1;
        if (!s.empty() && (s[0] == '-' || s[0] == '+')) { if (s[0] == '-') sg = -1; s.erase(0, 1); }
        return sg;
    };
    if (std::regex_match(t, re_int_bin) || std::regex_match(t, re_int_hex) || std::regex_match(t, re_int_dec) ||
        std::regex_match(t, re_int_oct) || std::regex_match(t, re_int_sex)) {
        try {
            std::string s = no_underscores(t);
            int sg = sign_of(s);
            long long n = 0;
            if (s == "0") n = 0;
            else if (starts_with(s, "0b")) n = std::stoll(s.substr(2), nullptr, 2);
            else if (starts_with(s, "0x")) n = std::stoll(s.substr(2), nullptr, 16);
            else if (s[0] == '0') n = std::stoll(s, nullptr, 8);
            else if (s.find(':') != std::string::npos) {
                for (auto& part : split(s, ':')) n = n * 60 + std::stoll(part);
            } else n = std::stoll(s);
            v.type = Value::Int;
            v.i = sg * n;
            return v;
        } catch (...) {
            return Value::str(t);
        }
    }
    if (std::regex_match(t, re_float)) {
        try {
            std::string s = lower(no_underscores(t));
            int sg = sign_of(s);
            v.type = Value::Float;
            if (s == ".inf") v.f = sg * INFINITY;
            else if (s == ".nan") v.f = NAN;
            else if (s.find(':') != std::string::npos) {
                double n = 0;
                for (auto& part : split(s, ':')) n = n * 60 + std::stod(part);
                v.f = sg * n;
            } else v.f = sg * std::stod(s);
            return v;
        } catch (...) {
            return Value::str(t);
        }
    }
    return Value::str(t);
}

// ---------------------------------------------------------------- parser ----

namespace {

static const int YAML_MAX_DEPTH = 64;
static const size_t YAML_MAX_ALIAS_NODES = 100000;

size_t value_nodes(const Value& v) {
    size_t n = 1;
    for (auto& x : v.list) n += value_nodes(x);
    for (auto& kv : v.map) n += value_nodes(kv.second);
    if (v.op) n += value_nodes(*v.op);
    return n;
}

const char* TAGS[] = {"append", "append-once", "prepend", "prepend-once", "remove", "scale", "offset", "set",
                      "append-from", "prepend-from"};

struct Line {
    int indent;
    std::string text;   // without indentation and comment
    int no;             // 1-based line number
};

struct YamlError {
    std::string msg;
};

[[noreturn]] void fail(int line, const std::string& m) {
    throw YamlError{"line " + std::to_string(line) + ": " + m};
}

using Anchors = std::map<std::string, Value>;

Value alias(const Anchors& a, const std::string& name, int line, size_t& budget) {
    auto it = a.find(name);
    if (it == a.end()) fail(line, "unknown alias *" + name);
    size_t cost = value_nodes(it->second);
    if (cost > budget) fail(line, "alias expansion limit exceeded");
    budget -= cost;
    return it->second;
}

// A mapping from its pairs in order; "<<" merges mappings in as PyYAML does: merged keys first (of a list, the
// first mapping wins), then the mapping's own keys.
Value finish_map(std::vector<std::pair<std::string, Value>>& pairs, int line) {
    std::vector<const Value*> merges;
    for (auto& kv : pairs)
        if (kv.first == "<<") {
            if (kv.second.is_map()) merges.push_back(&kv.second);
            else if (kv.second.is_list()) {
                for (auto it = kv.second.list.rbegin(); it != kv.second.list.rend(); ++it) {
                    if (!it->is_map()) fail(line, "<< takes a mapping or a list of mappings");
                    merges.push_back(&*it);
                }
            } else fail(line, "<< takes a mapping or a list of mappings");
        }
    Value m;
    m.type = Value::Dict;
    for (auto* mv : merges)
        for (auto& kv : mv->map) m.set(kv.first, kv.second);
    for (auto& kv : pairs)
        if (kv.first != "<<") m.set(kv.first, kv.second);
    return m;
}

// Removes a comment: '#' at the start or after a space, outside quotes.
std::string strip_comment(const std::string& s) {
    char q = 0;
    for (size_t k = 0; k < s.size(); k++) {
        char c = s[k];
        if (q) {
            if (c == q) {
                if (q == '\'' && k + 1 < s.size() && s[k + 1] == '\'') { k++; continue; }
                q = 0;
            } else if (q == '"' && c == '\\') k++;
            continue;
        }
        if ((c == '"' || c == '\'') && (k == 0 || std::strchr(" \t[{,:-", s[k - 1]))) { q = c; continue; }
        if (c == '#' && (k == 0 || s[k - 1] == ' ' || s[k - 1] == '\t')) return s.substr(0, k);
    }
    return s;
}

std::string rstrip(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    return s;
}

Value make_op(const std::string& tag, Value v, int line) {
    std::string name = tag.substr(1);
    bool known = false;
    for (auto* t : TAGS) known |= name == t;
    if (!known) fail(line, "unknown tag " + tag);
    Value o;
    o.type = Value::Op;
    o.s = name;
    o.op = std::make_shared<Value>(std::move(v));
    return o;
}

// -------------------------------------------------------- flow parser ----

struct Flow {
    const std::string& s;
    size_t i = 0;
    int line;
    Anchors* anchors = nullptr;
    size_t* budget = nullptr;

    std::string name_after_sigil() {
        size_t a = ++i;
        while (i < s.size() && !std::strchr(" ,]}\n", s[i])) i++;
        if (i == a) fail(line, "a name is expected after & or *");
        return s.substr(a, i - a);
    }

    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) i++; }

    Value quoted() {
        char q = s[i++];
        std::string out;
        for (;;) {
            if (i >= s.size()) fail(line, "unclosed quote");
            char c = s[i++];
            if (c == q) {
                if (q == '\'' && i < s.size() && s[i] == '\'') { out += '\''; i++; continue; }
                break;
            }
            if (q == '"' && c == '\\') {
                if (i >= s.size()) fail(line, "unclosed quote");
                char e = s[i++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case '0': out += '\0'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case ' ': out += ' '; break;
                    case 'x': case 'u': case 'U': {
                        size_t len = e == 'x' ? 2 : e == 'u' ? 4 : 8;
                        if (i + len > s.size()) fail(line, "incomplete hex escape");
                        std::string hex = s.substr(i, len);
                        for (char hc : hex)
                            if (!std::isxdigit((unsigned char)hc)) fail(line, "invalid hex escape \\" + std::string(1, e) + hex);
                        uint32_t cp = (uint32_t)std::stoul(hex, nullptr, 16);
                        i += len;
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                        else if (cp < 0x10000) {
                            out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else {
                            out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: fail(line, std::string("unknown escape \\") + e);
                }
                continue;
            }
            if (c == '\n') {                       // a line break inside quotes folds into a space
                while (!out.empty() && out.back() == ' ') out.pop_back();
                while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
                out += ' ';
                continue;
            }
            out += c;
        }
        return Value::str(out);
    }

    // A plain scalar inside flow: up to , ] } (and ': ' / ':' before them in a key).
    std::string plain(bool key) {
        size_t a = i;
        while (i < s.size()) {
            char c = s[i];
            if (c == ',' || c == ']' || c == '}' || c == '\n') break;
            if (c == ':' && (i + 1 >= s.size() || std::strchr(" ,]}\n", s[i + 1]))) break;
            if (c == ':' && key) break;
            i++;
        }
        return trim(s.substr(a, i - a));
    }

    Value value(int depth = 0) {
        if (depth > YAML_MAX_DEPTH) fail(line, "nesting too deep");
        ws();
        if (i >= s.size()) return Value();
        char c = s[i];
        if (c == '!') {
            size_t a = i;
            while (i < s.size() && !std::strchr(" ,]}\n", s[i])) i++;
            std::string tag = s.substr(a, i - a);
            ws();
            Value inner;
            if (i < s.size() && !std::strchr(",]}", s[i])) {
                if (s[i] == '{' || s[i] == '[') inner = value(depth + 1);
                else if (s[i] == '"' || s[i] == '\'') inner = quoted();
                else inner = Value::str(plain(false));
            }
            return make_op(tag, inner, line);
        }
        if (c == '*') return alias(*anchors, name_after_sigil(), line, *budget);
        if (c == '&') {
            std::string name = name_after_sigil();
            Value v = value(depth + 1);
            (*anchors)[name] = v;
            return v;
        }
        if (c == '{') {
            i++;
            std::vector<std::pair<std::string, Value>> pairs;
            for (;;) {
                ws();
                if (i >= s.size()) fail(line, "unclosed {");
                if (s[i] == '}') { i++; break; }
                std::string k;
                if (s[i] == '"' || s[i] == '\'') k = quoted().s;
                else k = plain(true);
                ws();
                Value v;
                if (i < s.size() && s[i] == ':') { i++; v = value(depth + 1); }
                pairs.emplace_back(k, v);
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == '}') { i++; break; }
                fail(line, "',' or '}' expected in {...}");
            }
            return finish_map(pairs, line);
        }
        if (c == '[') {
            i++;
            Value l;
            l.type = Value::List;
            for (;;) {
                ws();
                if (i >= s.size()) fail(line, "unclosed [");
                if (s[i] == ']') { i++; break; }
                l.list.push_back(value(depth + 1));
                ws();
                if (i < s.size() && s[i] == ',') { i++; continue; }
                if (i < s.size() && s[i] == ']') { i++; break; }
                fail(line, "',' or ']' expected in [...]");
            }
            return l;
        }
        if (c == '"' || c == '\'') return quoted();
        return yaml_resolve_scalar(plain(false));
    }
};

// ------------------------------------------------------- block parser ----

struct Block {
    std::vector<Line> L;
    Anchors anchors;
    size_t alias_budget = YAML_MAX_ALIAS_NODES;
    size_t n() const { return L.size(); }

    static bool is_seq_item(const std::string& t) { return t == "-" || starts_with(t, "- "); }

    // Position of the ':' that ends a mapping key, or npos.
    static size_t key_colon(const std::string& t) {
        if (t.empty() || t[0] == '{' || t[0] == '[' || t[0] == '!' || t[0] == '|' || t[0] == '>') return std::string::npos;
        size_t k = 0;
        if (t[0] == '"' || t[0] == '\'') {           // quoted key
            char q = t[0];
            for (k = 1; k < t.size(); k++) {
                if (t[k] == '\\' && q == '"') { k++; continue; }
                if (t[k] == q) {
                    if (q == '\'' && k + 1 < t.size() && t[k + 1] == '\'') { k++; continue; }
                    break;
                }
            }
            size_t c = k + 1;
            while (c < t.size() && t[c] == ' ') c++;
            return (c < t.size() && t[c] == ':' && (c + 1 == t.size() || t[c + 1] == ' ')) ? c : std::string::npos;
        }
        for (; k < t.size(); k++)
            if (t[k] == ':' && (k + 1 == t.size() || t[k + 1] == ' ')) return k;
        return std::string::npos;
    }

    static std::string key_text(const std::string& raw, int line) {
        std::string k = trim(raw);
        if (!k.empty() && (k[0] == '"' || k[0] == '\'')) {
            Flow f{k, 0, line, nullptr, nullptr};
            return f.quoted().s;
        }
        if (!k.empty() && (k[0] == '&' || k[0] == '*' || k[0] == '?')) fail(line, "anchors, aliases and ? as keys are not supported");
        // a plain key is resolved like any scalar (yes -> True ...) and then used as text
        Value v = yaml_resolve_scalar(k);
        if (v.type == Value::Bool) return v.b ? "True" : "False";
        if (v.type == Value::Null) return "None";
        if (v.type == Value::Int) return std::to_string(v.i);
        if (v.type == Value::Float) return py_repr_double(v.f);
        return k;
    }

    Value node(size_t& idx, int indent, int depth = 0) {
        if (depth > YAML_MAX_DEPTH) fail(L[idx].no, "nesting too deep");
        const std::string& t = L[idx].text;
        if (is_seq_item(t)) return seq(idx, indent, depth);
        if (key_colon(t) != std::string::npos) return mapping(idx, indent, depth);
        std::string rest = t;
        int line = L[idx].no;
        idx++;
        return inline_value(rest, idx, indent - 1, line, false, depth);
    }

    Value mapping(size_t& idx, int indent, int depth) {
        std::vector<std::pair<std::string, Value>> pairs;
        int first = idx < n() ? L[idx].no : 0;
        while (idx < n() && L[idx].indent == indent && !is_seq_item(L[idx].text)) {
            const std::string& t = L[idx].text;
            size_t c = key_colon(t);
            if (c == std::string::npos) fail(L[idx].no, "'key: value' expected");
            int line = L[idx].no;
            std::string key = key_text(t.substr(0, c), line);
            std::string rest = trim(t.substr(c + 1));
            idx++;
            pairs.emplace_back(key, inline_value(rest, idx, indent, line, true, depth + 1));
        }
        if (idx < n() && L[idx].indent > indent) fail(L[idx].no, "unexpected indentation");
        return finish_map(pairs, first);
    }

    Value seq(size_t& idx, int indent, int depth) {
        Value l;
        l.type = Value::List;
        while (idx < n() && L[idx].indent == indent && is_seq_item(L[idx].text)) {
            std::string t = L[idx].text;
            size_t k = 1;
            while (k < t.size() && t[k] == ' ') k++;
            std::string rest = t.substr(k);
            int line = L[idx].no;
            if (rest.empty()) {
                idx++;
                l.list.push_back(inline_value("", idx, indent, line, false, depth + 1));
            } else if (is_seq_item(rest) || key_colon(rest) != std::string::npos) {
                L[idx] = Line{indent + (int)k, rest, line};       // "- key: v" opens a mapping at that column
                l.list.push_back(node(idx, indent + (int)k, depth + 1));
            } else {
                idx++;
                l.list.push_back(inline_value(rest, idx, indent, line, false, depth + 1));
            }
        }
        return l;
    }

    // The value after "key:" or "- ". parent = the indentation of the key / dash.
    Value inline_value(std::string rest, size_t& idx, int parent, int line, bool in_map = false, int depth = 0) {
        if (depth > YAML_MAX_DEPTH) fail(line, "nesting too deep");
        std::string tag, anchor;
        for (int k = 0; k < 2; k++) {                  // "!tag &anchor value" or "&anchor !tag value"
            if (!rest.empty() && (rest[0] == '!' || rest[0] == '&')) {
                size_t e = rest.find(' ');
                std::string part = rest.substr(0, e);
                rest = e == std::string::npos ? "" : trim(rest.substr(e + 1));
                if (part[0] == '!') tag = part;
                else anchor = part.substr(1);
            }
        }
        Value v = inline_plain(rest, tag, idx, parent, line, in_map, depth);
        if (!anchor.empty()) anchors[anchor] = v;
        return v;
    }

    Value inline_plain(std::string rest, const std::string& tag, size_t& idx, int parent, int line, bool in_map,
                       int depth) {
        Value v;
        if (rest.empty()) {
            if (idx < n() && (L[idx].indent > parent || (in_map && L[idx].indent == parent && is_seq_item(L[idx].text))))
                v = node(idx, L[idx].indent, depth + 1);
        } else if (rest[0] == '{' || rest[0] == '[' || rest[0] == '"' || rest[0] == '\'') {
            // flow or quoted: may go on over the next lines
            std::string text = rest;
            for (;;) {
                Flow f{text, 0, line, &anchors, &alias_budget};
                try {
                    v = f.value(depth + 1);
                    f.ws();
                    if (f.i != text.size()) fail(line, "unexpected text after a value: " + text.substr(f.i));
                    break;
                } catch (YamlError& e) {
                    bool open = e.msg.find("unclosed") != std::string::npos;
                    if (!open || idx >= n()) throw;
                    text += "\n" + L[idx].text;
                    idx++;
                }
            }
            if (!tag.empty() && v.type == Value::Str) v = Value::str(v.s);
        } else if (rest[0] == '|' || rest[0] == '>') {
            bool fold = rest[0] == '>';
            std::string out;
            int bi = -1;
            while (idx < n() && L[idx].indent > parent) {
                if (bi < 0) bi = L[idx].indent;
                std::string part = std::string((size_t)(L[idx].indent - bi), ' ') + L[idx].text;
                if (!out.empty()) out += fold ? " " : "\n";
                out += part;
                idx++;
            }
            if (rest.find('-') == std::string::npos) out += "\n";
            v = Value::str(out);
        } else if (rest[0] == '*') {
            if (!tag.empty()) fail(line, "a tag before an alias is not supported");
            v = alias(anchors, trim(rest.substr(1)), line, alias_budget);
        } else {
            std::string text = rest;                       // plain scalar, may continue on deeper lines
            while (idx < n() && L[idx].indent > parent && !in_map_key(L[idx].text)) {
                text += " " + L[idx].text;
                idx++;
            }
            v = tag.empty() ? yaml_resolve_scalar(text) : Value::str(text);
        }
        if (!tag.empty()) {
            if (v.type == Value::Str && v.s.empty() && rest.empty()) v = Value();
            return make_op(tag, v, line);
        }
        return v;
    }

    static bool in_map_key(const std::string& t) { return is_seq_item(t) || key_colon(t) != std::string::npos; }
};

}  // namespace

std::vector<Value> yaml_load_all(const std::string& src, std::string& error) {
    std::vector<Value> docs;
    std::vector<std::vector<Line>> parts(1);
    std::string text = starts_with(src, "\xEF\xBB\xBF") ? src.substr(3) : src;
    std::istringstream in(text);
    std::string raw;
    int no = 0;
    while (std::getline(in, raw)) {
        no++;
        raw = rstrip(raw);
        if (raw == "---" || starts_with(raw, "--- ")) {
            parts.emplace_back();
            if (raw.size() > 4) parts.back().push_back(Line{0, trim(raw.substr(4)), no});
            continue;
        }
        if (raw == "...") { parts.emplace_back(); continue; }
        size_t ind = 0;
        while (ind < raw.size() && raw[ind] == ' ') ind++;
        if (ind < raw.size() && raw[ind] == '\t') { error = "line " + std::to_string(no) + ": tab in indentation"; return {}; }
        std::string t = rstrip(strip_comment(raw.substr(ind)));
        if (t.empty()) continue;
        if (t[0] == '%') continue;                 // directives
        parts.back().push_back(Line{(int)ind, t, no});
    }
    try {
        for (auto& p : parts) {
            if (p.empty()) continue;
            Block b{p, {}};
            size_t idx = 0;
            Value v = b.node(idx, b.L[0].indent);
            if (idx < b.n()) fail(b.L[idx].no, "unexpected text");
            // PyYAML documents that are empty or false are skipped (ItemXL's load_all keeps `if d`)
            bool empty = v.is_null() || (v.is_map() && v.map.empty()) || (v.is_list() && v.list.empty()) ||
                         (v.type == Value::Bool && !v.b) || (v.type == Value::Int && v.i == 0) ||
                         (v.type == Value::Str && v.s.empty());
            if (!empty) docs.push_back(std::move(v));
        }
    } catch (YamlError& e) {
        error = e.msg;
        return {};
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
    return docs;
}

}  // namespace itemxl
