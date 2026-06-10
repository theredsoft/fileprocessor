#include "core/template/template.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "core/template/textutil.h"

namespace fp::tmpl {
namespace {

struct Word {
    std::string text;  // for quoted words: contents without the quotes
    size_t pos = 0;    // byte offset in the whole template string
    bool quoted = false;
    bool isEquals = false;
};

bool iequals(const std::string& a, const char* b) {
    const size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

bool isKeyword(const Word& w, const char* kw) { return !w.quoted && !w.isEquals && iequals(w.text, kw); }

std::vector<Word> lexWords(std::string_view body, size_t base) {
    std::vector<Word> words;
    size_t i = 0;
    const size_t n = body.size();
    while (i < n) {
        const char c = body[i];
        if (c == ' ' || c == '\t') {
            ++i;
            continue;
        }
        if (c == '=') {
            words.push_back({"=", base + i, false, true});
            ++i;
            continue;
        }
        if (c == '"') {
            size_t j = i + 1;
            while (j < n && body[j] != '"') ++j;
            if (j >= n) throw ParseError(base + i, "unterminated quoted text");
            words.push_back({std::string(body.substr(i + 1, j - i - 1)), base + i, true, false});
            i = j + 1;
            continue;
        }
        size_t j = i;
        while (j < n && body[j] != ' ' && body[j] != '\t' && body[j] != '=' && body[j] != '"') ++j;
        words.push_back({std::string(body.substr(i, j - i)), base + i, false, false});
        i = j;
    }
    return words;
}

bool parseLong(const std::string& s, long& out) {
    if (s.empty()) return false;
    size_t k = 0;
    bool neg = false;
    if (s[0] == '-' || s[0] == '+') {
        neg = (s[0] == '-');
        k = 1;
        if (s.size() == 1) return false;
    }
    long v = 0;
    for (; k < s.size(); ++k) {
        if (!std::isdigit(static_cast<unsigned char>(s[k]))) return false;
        v = v * 10 + (s[k] - '0');
        if (v > 1000000000000L) return false;
    }
    out = neg ? -v : v;
    return true;
}

bool allDigits(const std::string& s) {
    return !s.empty() &&
           std::all_of(s.begin(), s.end(),
                       [](unsigned char c) { return std::isdigit(c) != 0; });
}

std::optional<Source> sourceByName(const std::string& w) {
    if (iequals(w, "folder")) return Source::Folder;
    if (iequals(w, "name")) return Source::Name;
    if (iequals(w, "ext")) return Source::Ext;
    if (iequals(w, "number")) return Source::Number;
    if (iequals(w, "filedate")) return Source::FileDate;
    if (iequals(w, "photodate")) return Source::PhotoDate;
    return std::nullopt;
}

bool maskHasPlaceholder(const std::string& m) {
    static const char* placeholders[] = {"YYYY", "YY", "MM", "DD", "hh", "mm", "ss"};
    for (const char* p : placeholders)
        if (m.find(p) != std::string::npos) return true;
    return false;
}

// Splits a word list on a keyword (OR / AND); empty segments are an error.
std::vector<std::vector<Word>> splitByKeyword(const std::vector<Word>& words,
                                              const char* kw, const char* display) {
    std::vector<std::vector<Word>> out(1);
    size_t lastKwPos = words.front().pos;
    for (const auto& w : words) {
        if (isKeyword(w, kw)) {
            if (out.back().empty())
                throw ParseError(w.pos, std::string("expected an expression before ") + display);
            lastKwPos = w.pos;
            out.emplace_back();
        } else {
            out.back().push_back(w);
        }
    }
    if (out.back().empty())
        throw ParseError(lastKwPos, std::string("expected an expression after ") + display);
    return out;
}

Modifier parseModifier(const std::vector<Word>& part, Source src) {
    const Word& w = part[0];
    if (w.quoted || w.isEquals)
        throw ParseError(w.pos, "expected a modifier keyword after AND "
                                "(uppercase, lowercase, nospaces, first or step)");
    auto noArgs = [&](ModKind k) {
        if (part.size() > 1)
            throw ParseError(part[1].pos, "'" + w.text + "' takes no value");
        return Modifier{k, 0};
    };
    if (iequals(w.text, "uppercase")) return noArgs(ModKind::Uppercase);
    if (iequals(w.text, "lowercase")) return noArgs(ModKind::Lowercase);
    if (iequals(w.text, "nospaces")) return noArgs(ModKind::NoSpaces);

    const bool isFirst = iequals(w.text, "first");
    const bool isStep = iequals(w.text, "step");
    if (isFirst || isStep) {
        const std::string usage = isFirst ? "first 8" : "step = 2";
        size_t i = 1;
        if (i < part.size() && part[i].isEquals) ++i;
        long v = 0;
        if (i >= part.size() || !parseLong(part[i].text, v))
            throw ParseError(w.pos, "'" + w.text + "' expects a number, e.g. '" + usage + "'");
        if (i + 1 < part.size())
            throw ParseError(part[i + 1].pos, "unexpected '" + part[i + 1].text + "'");
        if (isFirst) {
            if (v <= 0) throw ParseError(part[i].pos, "'first' expects a positive number");
            return Modifier{ModKind::First, v};
        }
        if (src != Source::Number)
            throw ParseError(w.pos, "'step' is only valid for {number}");
        if (v == 0) throw ParseError(part[i].pos, "'step' must not be zero");
        return Modifier{ModKind::Step, v};
    }
    throw ParseError(w.pos, "unknown modifier '" + w.text +
                            "' (expected uppercase, lowercase, nospaces, first or step)");
}

Variant parseVariant(const std::vector<std::vector<Word>>& parts) {
    const auto& head = parts[0];
    const Word& w0 = head[0];
    Variant v;

    if (w0.quoted) {
        v.source = Source::Literal;
        v.literal = w0.text;
        if (head.size() > 1)
            throw ParseError(head[1].pos, "unexpected text after quoted value");
        if (parts.size() > 1)
            throw ParseError(parts[1][0].pos, "quoted text cannot have modifiers");
        return v;
    }
    if (w0.isEquals)
        throw ParseError(w0.pos, "expected a source before '=' "
                                 "(folder, name, ext, number, filedate or photodate)");

    const auto src = sourceByName(w0.text);
    if (!src)
        throw ParseError(w0.pos, "unknown source '" + w0.text +
                                 "' (expected folder, name, ext, number, filedate or photodate)");
    v.source = *src;

    size_t i = 1;
    if (i < head.size()) {
        if (!head[i].isEquals)
            throw ParseError(head[i].pos,
                             "expected '=', AND, OR or '}' after '" + w0.text + "'");
        ++i;
        if (i >= head.size() || head[i].isEquals)
            throw ParseError(head[i - 1].pos, "expected a format value after '='");
        v.format = head[i].text;
        ++i;
        if (i < head.size())
            throw ParseError(head[i].pos, "unexpected '" + head[i].text + "'");
    }

    if (v.format) {
        switch (v.source) {
        case Source::Number:
            if (!allDigits(*v.format) || v.format->size() > 12)
                throw ParseError(w0.pos,
                                 "number format must be a digits-only sample like 0001");
            v.numberStart = std::stol(*v.format);
            v.numberWidth = static_cast<int>(v.format->size());
            break;
        case Source::FileDate:
        case Source::PhotoDate:
            if (!maskHasPlaceholder(*v.format))
                throw ParseError(w0.pos,
                                 "date format must contain YYYY, YY, MM, DD, hh, mm or ss");
            break;
        default:
            throw ParseError(w0.pos, "'=' format is not supported for {" + w0.text + "}");
        }
    }

    for (size_t p = 1; p < parts.size(); ++p)
        v.mods.push_back(parseModifier(parts[p], v.source));
    return v;
}

std::vector<Variant> parseTokenBody(std::string_view body, size_t base) {
    const auto words = lexWords(body, base);
    if (words.empty())
        throw ParseError(base, "empty token: expected a source "
                               "(folder, name, ext, number, filedate or photodate)");
    std::vector<Variant> variants;
    for (const auto& group : splitByKeyword(words, "or", "OR"))
        variants.push_back(parseVariant(splitByKeyword(group, "and", "AND")));
    return variants;
}

std::string padNumber(long v, int width) {
    if (v < 0) return std::to_string(v);  // zero-padding makes no sense below zero
    std::string digits = std::to_string(v);
    while (static_cast<int>(digits.size()) < width) digits.insert(digits.begin(), '0');
    return digits;
}

std::string formatDate(const DateTime& dt, const std::string& mask) {
    std::string out;
    out.reserve(mask.size() + 4);
    auto append = [&out](const char* fmt, int v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), fmt, v);
        out += buf;
    };
    size_t i = 0;
    while (i < mask.size()) {
        if (mask.compare(i, 4, "YYYY") == 0) { append("%04d", dt.year); i += 4; }
        else if (mask.compare(i, 2, "YY") == 0) { append("%02d", dt.year % 100); i += 2; }
        else if (mask.compare(i, 2, "MM") == 0) { append("%02d", dt.month); i += 2; }
        else if (mask.compare(i, 2, "DD") == 0) { append("%02d", dt.day); i += 2; }
        else if (mask.compare(i, 2, "hh") == 0) { append("%02d", dt.hour); i += 2; }
        else if (mask.compare(i, 2, "mm") == 0) { append("%02d", dt.minute); i += 2; }
        else if (mask.compare(i, 2, "ss") == 0) { append("%02d", dt.second); i += 2; }
        else { out += mask[i]; ++i; }
    }
    return out;
}

std::optional<std::string> evalVariant(const Variant& v, const FileContext& ctx,
                                       std::string& whyNot) {
    std::string value;
    switch (v.source) {
    case Source::Literal: value = v.literal; break;
    case Source::Folder: value = ctx.folderName; break;
    case Source::Name: value = ctx.stem; break;
    case Source::Ext: value = ctx.ext; break;
    case Source::Number: {
        long step = 1;
        for (const auto& m : v.mods)
            if (m.kind == ModKind::Step) step = m.arg;
        value = padNumber(v.numberStart + ctx.index * step, v.numberWidth);
        break;
    }
    case Source::FileDate:
        if (!ctx.fileDate) {
            whyNot = "file date is unavailable";
            return std::nullopt;
        }
        value = formatDate(*ctx.fileDate, v.format.value_or("YYYY-MM-DD"));
        break;
    case Source::PhotoDate:
        if (!ctx.photoDate) {
            whyNot = "photo date (EXIF) is unavailable";
            return std::nullopt;
        }
        value = formatDate(*ctx.photoDate, v.format.value_or("YYYY-MM-DD"));
        break;
    }
    for (const auto& m : v.mods) {
        switch (m.kind) {
        case ModKind::Uppercase: value = fp::text::toUpper(value); break;
        case ModKind::Lowercase: value = fp::text::toLower(value); break;
        case ModKind::NoSpaces: value = fp::text::removeSpaces(value); break;
        case ModKind::First: value = fp::text::firstCodepoints(value, m.arg); break;
        case ModKind::Step: break;  // consumed by the number computation above
        }
    }
    return value;
}

}  // namespace

Template Template::parse(std::string_view text) {
    Template t;
    std::string literal;
    size_t litPos = 0;
    auto flush = [&] {
        if (!literal.empty()) {
            Node node;
            node.isText = true;
            node.text = std::move(literal);
            node.pos = litPos;
            t.nodes_.push_back(std::move(node));
            literal.clear();
        }
    };
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if (c == '{') {
            if (i + 1 < n && text[i + 1] == '{') {
                if (literal.empty()) litPos = i;
                literal += '{';
                i += 2;
                continue;
            }
            const size_t start = i + 1;
            size_t j = start;
            bool inQuotes = false;
            while (j < n && (inQuotes || text[j] != '}')) {
                if (text[j] == '"') inQuotes = !inQuotes;
                ++j;
            }
            if (j >= n)
                throw ParseError(i, inQuotes ? "unterminated quoted text"
                                             : "missing '}' for this token");
            flush();
            Node node;
            node.isText = false;
            node.pos = i;
            node.variants = parseTokenBody(text.substr(start, j - start), start);
            t.nodes_.push_back(std::move(node));
            i = j + 1;
        } else if (c == '}') {
            if (i + 1 < n && text[i + 1] == '}') {
                if (literal.empty()) litPos = i;
                literal += '}';
                i += 2;
                continue;
            }
            throw ParseError(i, "unexpected '}' (use '}}' for a literal brace)");
        } else {
            if (literal.empty()) litPos = i;
            literal += c;
            ++i;
        }
    }
    flush();
    return t;
}

bool Template::uses(Source source) const {
    for (const auto& node : nodes_) {
        if (node.isText) continue;
        for (const auto& v : node.variants)
            if (v.source == source) return true;
    }
    return false;
}

std::string Template::apply(const FileContext& ctx) const {
    std::string out;
    for (const auto& node : nodes_) {
        if (node.isText) {
            out += node.text;
            continue;
        }
        bool done = false;
        std::string reasons;
        for (const auto& v : node.variants) {
            std::string whyNot;
            if (auto value = evalVariant(v, ctx, whyNot)) {
                out += *value;
                done = true;
                break;
            }
            if (!reasons.empty()) reasons += "; ";
            reasons += whyNot;
        }
        if (!done)
            throw EvalError(node.pos,
                            reasons.empty() ? "token has no value" : reasons);
    }
    return out;
}

}  // namespace fp::tmpl
