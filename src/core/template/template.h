#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fp::tmpl {

struct DateTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

// Everything the template engine may need to know about one file.
// Strings are UTF-8; the UI layer converts from QString / wide paths.
struct FileContext {
    std::string folderName;             // name of the containing folder
    std::string stem;                   // file name without extension
    std::string ext;                    // extension without the leading dot
    long index = 0;                     // 0-based position in the processed list
    std::optional<DateTime> fileDate;   // file modification time
    std::optional<DateTime> photoDate;  // EXIF DateTimeOriginal
};

struct ParseError : std::runtime_error {
    size_t pos;  // byte offset in the template string
    ParseError(size_t p, const std::string& msg) : std::runtime_error(msg), pos(p) {}
};

struct EvalError : std::runtime_error {
    size_t pos;  // byte offset of the failing token in the template string
    EvalError(size_t p, const std::string& msg) : std::runtime_error(msg), pos(p) {}
};

enum class Source { Folder, Name, Ext, Number, FileDate, PhotoDate, Literal };

enum class ModKind { Uppercase, Lowercase, NoSpaces, First, Step };

struct Modifier {
    ModKind kind;
    long arg = 0;  // First: codepoint count, Step: increment
};

struct Variant {
    Source source = Source::Literal;
    std::string literal;                // Source::Literal payload
    std::optional<std::string> format;  // raw text after '='
    long numberStart = 1;               // derived from a sample like 0010
    int numberWidth = 1;
    std::vector<Modifier> mods;
};

struct Node {
    bool isText = true;
    std::string text;               // isText payload
    std::vector<Variant> variants;  // token payload, tried left to right
    size_t pos = 0;                 // byte offset in the template string
};

class Template {
public:
    static Template parse(std::string_view text);     // throws ParseError
    std::string apply(const FileContext& ctx) const;  // throws EvalError

    // True if any variant references the source — lets callers skip
    // expensive lookups (EXIF) for templates that never need them.
    bool uses(Source source) const;

    const std::vector<Node>& nodes() const { return nodes_; }

private:
    std::vector<Node> nodes_;
};

}  // namespace fp::tmpl
