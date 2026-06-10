#pragma once

#include <string>
#include <vector>

#include "core/template/template.h"

namespace fp::plan {

enum class Issue {
    None,                // valid rename
    Unchanged,           // new name equals the old one — nothing to do, not an error
    EvalFailed,          // template evaluation failed (e.g. no EXIF date)
    EmptyName,
    InvalidChar,         // \ / : * ? " < > | or control characters
    ReservedName,        // CON, PRN, AUX, NUL, COM1-9, LPT1-9
    TrailingDotOrSpace,  // Windows silently strips these — forbid instead
    TooLong,             // > 255 characters
    NameClash,           // several files end up with the same name
};

struct SourceFile {
    std::string name;       // current file name
    tmpl::FileContext ctx;  // input for the template engine
};

struct PlanItem {
    std::string oldName;
    std::string newName;  // empty when evaluation failed
    Issue issue = Issue::None;
    std::string message;  // human-readable detail for non-None issues
};

struct Plan {
    std::vector<PlanItem> items;  // same order as the input
    size_t renameCount = 0;       // items that will actually be renamed
    size_t conflictCount = 0;     // blocking issues (everything except Unchanged)

    bool executable() const { return conflictCount == 0 && renameCount > 0; }
};

// Pure function: no filesystem access. The folder contents must be passed in
// full — clash detection assumes `files` lists every file in the folder.
// Name comparison is case-insensitive, as on Windows filesystems.
Plan buildPlan(const tmpl::Template& tmpl, const std::vector<SourceFile>& files);

// Exposed separately for tests; fills `message` and returns the issue
// (Issue::None when `name` is a valid Windows file name).
Issue validateWindowsName(const std::string& name, std::string& message);

}  // namespace fp::plan
