// Small helpers: strings, hashes, files.
#include "itemxl.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace itemxl {

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    // Python str.strip(): ASCII whitespace and Unicode spaces (such as U+00A0 NBSP and U+2000..U+200A in the game's XMLs)
    auto ws_len = [&](size_t i) -> size_t {
        unsigned char c = (unsigned char)s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') return 1;
        if (c == 0xC2 && i + 1 < s.size() && (unsigned char)s[i + 1] == 0xA0) return 2;
        if (c == 0xE2 && i + 2 < s.size() && (unsigned char)s[i + 1] == 0x80) {
            unsigned char b2 = (unsigned char)s[i + 2];
            if ((b2 >= 0x80 && b2 <= 0x8A) || b2 == 0xA8 || b2 == 0xA9 || b2 == 0xAF) return 3;
        }
        if (c == 0xE3 && i + 2 < s.size() && (unsigned char)s[i + 1] == 0x80 && (unsigned char)s[i + 2] == 0x80) return 3;
        return 0;
    };
    size_t a = 0, b = s.size();
    for (;;) {
        size_t n = a < b ? ws_len(a) : 0;
        if (!n) break;
        a += n;
    }
    while (b > a) {
        size_t n = 0;
        if (ws_len(b - 1) == 1) n = 1;
        else if (b - a >= 2 && ws_len(b - 2) == 2) n = 2;
        else if (b - a >= 3 && ws_len(b - 3) == 3) n = 3;
        if (!n) break;
        b -= n;
    }
    return s.substr(a, b - a);
}

bool starts_with(const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; }

bool ends_with(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        size_t b = s.find(sep, a);
        out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (unsigned char c : s) h = (h ^ c) * 0x100000001B3ull;
    return h;
}

uint32_t crc32(const uint8_t* data, size_t n, uint32_t crc) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

std::string fmt_double(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.6f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s.empty() || s == "-0") return "0";
    return s;
}

static std::filesystem::path u8path(const std::string& p) {
    return std::filesystem::u8path(p);
}

bool read_file(const std::string& path, Bytes& out) {
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    f.seekg(0);
    out.resize((size_t)n);
    if (n > 0) f.read((char*)out.data(), n);
    return (bool)f;
}

bool write_file(const std::string& path, const void* data, size_t n) {
    std::ofstream f(u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write((const char*)data, (std::streamsize)n);
    return (bool)f;
}

void make_dirs(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(u8path(dir), ec);
}

}  // namespace itemxl
