#include "core/template/textutil.h"

namespace fp::text {
namespace {

char32_t upperOf(char32_t c) {
    if (c >= U'a' && c <= U'z') return c - 32;
    if (c >= 0x00E0 && c <= 0x00FE && c != 0x00F7) return c - 32;  // à-þ → À-Þ
    if (c >= 0x0430 && c <= 0x044F) return c - 32;                 // а-я → А-Я
    if (c == 0x0451) return 0x0401;                                // ё → Ё
    return c;
}

char32_t lowerOf(char32_t c) {
    if (c >= U'A' && c <= U'Z') return c + 32;
    if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7) return c + 32;
    if (c >= 0x0410 && c <= 0x042F) return c + 32;
    if (c == 0x0401) return 0x0451;
    return c;
}

}  // namespace

std::u32string utf8Decode(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const unsigned char b = static_cast<unsigned char>(s[i]);
        char32_t cp = 0;
        int len = 0;
        if (b < 0x80) {
            cp = b;
            len = 1;
        } else if ((b & 0xE0) == 0xC0) {
            cp = b & 0x1F;
            len = 2;
        } else if ((b & 0xF0) == 0xE0) {
            cp = b & 0x0F;
            len = 3;
        } else if ((b & 0xF8) == 0xF0) {
            cp = b & 0x07;
            len = 4;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        if (i + len > n) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            const unsigned char cb = static_cast<unsigned char>(s[i + k]);
            if ((cb & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cb & 0x3F);
        }
        if (!ok) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(cp);
        i += len;
    }
    return out;
}

std::string utf8Encode(const std::u32string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char32_t c : s) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::string toUpper(std::string_view s) {
    auto u = utf8Decode(s);
    for (auto& c : u) c = upperOf(c);
    return utf8Encode(u);
}

std::string toLower(std::string_view s) {
    auto u = utf8Decode(s);
    for (auto& c : u) c = lowerOf(c);
    return utf8Encode(u);
}

std::string removeSpaces(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s)
        if (c != ' ' && c != '\t') out += c;
    return out;
}

std::string firstCodepoints(std::string_view s, long n) {
    if (n <= 0) return {};
    auto u = utf8Decode(s);
    if (static_cast<long>(u.size()) > n) u.resize(static_cast<size_t>(n));
    return utf8Encode(u);
}

}  // namespace fp::text
